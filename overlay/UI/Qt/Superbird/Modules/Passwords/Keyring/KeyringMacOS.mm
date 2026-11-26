/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Keyring/Keyring.h>

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <QCoreApplication>
#include <QMetaObject>

#include <dispatch/dispatch.h>
#include <memory>
#include <thread>

namespace Superbird::Passwords {

namespace {

NSString* to_nsstring(std::string_view s)
{
    return [[NSString alloc] initWithBytes:s.data() length:s.size() encoding:NSUTF8StringEncoding];
}

void post_to_gui(std::function<void()> fn)
{
    auto* app = QCoreApplication::instance();
    if (!app) {
        fn();
        return;
    }
    QMetaObject::invokeMethod(app, [fn = std::move(fn)]() mutable { fn(); }, Qt::QueuedConnection);
}

} // namespace

bool Keyring::is_available()
{
    return true;
}

bool Keyring::supports_biometrics()
{
    @autoreleasepool {
        LAContext* ctx = [[LAContext alloc] init];
        NSError* err = nil;
        BOOL ok = [ctx canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&err];
        return ok == YES;
    }
}

bool Keyring::store_secret(
    std::string_view service,
    std::string_view account,
    std::span<uint8_t const> secret,
    bool require_biometrics)
{
    if (service.empty() || account.empty() || secret.empty())
        return false;

    @autoreleasepool {
        delete_secret(service, account);

        NSString* ns_service = to_nsstring(service);
        NSString* ns_account = to_nsstring(account);
        NSData* ns_data = [NSData dataWithBytes:secret.data() length:secret.size()];

        if (require_biometrics && supports_biometrics()) {
            CFErrorRef acl_err = nullptr;
            SecAccessControlRef acl = SecAccessControlCreateWithFlags(
                kCFAllocatorDefault,
                kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
                kSecAccessControlBiometryCurrentSet,
                &acl_err);
            if (acl) {
                NSDictionary* query = @{
                    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
                    (__bridge id)kSecAttrService : ns_service,
                    (__bridge id)kSecAttrAccount : ns_account,
                    (__bridge id)kSecValueData : ns_data,
                    (__bridge id)kSecAttrAccessControl : (__bridge id)acl,
                };
                OSStatus status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
                CFRelease(acl);
                if (status == errSecSuccess)
                    return true;
                // Ad-hoc signed local builds may lack Keychain access group entitlements for SecAccessControl;
                // fall through to standard Keychain item gated by LAContext on load.
            }
            if (acl_err)
                CFRelease(acl_err);
        }

        NSDictionary* fallback_query = @{
            (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : ns_service,
            (__bridge id)kSecAttrAccount : ns_account,
            (__bridge id)kSecValueData : ns_data,
            (__bridge id)kSecAttrAccessible : (__bridge id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        };
        OSStatus status = SecItemAdd((__bridge CFDictionaryRef)fallback_query, nullptr);
        return status == errSecSuccess;
    }
}

void Keyring::load_secret_async(
    std::string_view service,
    std::string_view account,
    std::string_view prompt_reason,
    LoadCallback callback)
{
    std::string svc(service);
    std::string acct(account);
    std::string reason(prompt_reason.empty() ? "Unlock Superbird Passwords" : prompt_reason);

    auto cb = std::make_shared<LoadCallback>(std::move(callback));
    std::thread([svc = std::move(svc), acct = std::move(acct), reason = std::move(reason), cb = std::move(cb)] {
        @autoreleasepool {
            LAContext* ctx = [[LAContext alloc] init];
            NSError* auth_err = nil;
            bool auth_ok = true;

            // Gate Keychain retrieval with Touch ID / local owner authentication off the GUI thread.
            LAPolicy policy = [ctx canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&auth_err]
                ? LAPolicyDeviceOwnerAuthenticationWithBiometrics
                : LAPolicyDeviceOwnerAuthentication;

            if ([ctx canEvaluatePolicy:policy error:&auth_err]) {
                dispatch_semaphore_t sem = dispatch_semaphore_create(0);
                __block BOOL evaluated = NO;
                __block NSString* eval_error_msg = nil;

                [ctx evaluatePolicy:policy
                    localizedReason:to_nsstring(reason)
                              reply:^(BOOL success, NSError* error) {
                                  evaluated = success;
                                  if (!success && error)
                                      eval_error_msg = [error localizedDescription];
                                  dispatch_semaphore_signal(sem);
                              }];

                dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
                if (!evaluated) {
                    auth_ok = false;
                    std::string err_str = eval_error_msg ? std::string([eval_error_msg UTF8String]) : "Biometric authentication cancelled";
                    post_to_gui([cb, err_str = std::move(err_str)] {
                        if (*cb)
                            (*cb)(std::nullopt, err_str);
                    });
                    return;
                }
            }

            if (!auth_ok)
                return;

            NSDictionary* query = @{
                (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
                (__bridge id)kSecAttrService : to_nsstring(svc),
                (__bridge id)kSecAttrAccount : to_nsstring(acct),
                (__bridge id)kSecReturnData : @YES,
                (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
                (__bridge id)kSecUseAuthenticationContext : ctx,
            };

            CFTypeRef result = nullptr;
            OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
            if (status != errSecSuccess || !result) {
                if (result)
                    CFRelease(result);
                std::string err = (status == errSecItemNotFound)
                    ? "No saved biometric unlock key in Keychain"
                    : ("Keychain error (" + std::to_string(static_cast<int>(status)) + ")");
                post_to_gui([cb, err = std::move(err)] {
                    if (*cb)
                        (*cb)(std::nullopt, err);
                });
                return;
            }

            NSData* data = (__bridge_transfer NSData*)result;
            SecureBytes secret({ static_cast<uint8_t const*>(data.bytes), static_cast<size_t>(data.length) });
            post_to_gui([cb, secret = std::move(secret)]() mutable {
                if (*cb)
                    (*cb)(std::move(secret), {});
            });
        }
    }).detach();
}

bool Keyring::has_secret(std::string_view service, std::string_view account)
{
    if (service.empty() || account.empty())
        return false;
    @autoreleasepool {
        LAContext* ctx = [[LAContext alloc] init];
        ctx.interactionNotAllowed = YES;
        NSDictionary* query = @{
            (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : to_nsstring(service),
            (__bridge id)kSecAttrAccount : to_nsstring(account),
            (__bridge id)kSecReturnAttributes : @YES,
            (__bridge id)kSecMatchLimit : (__bridge id)kSecMatchLimitOne,
            (__bridge id)kSecUseAuthenticationContext : ctx,
        };
        CFTypeRef result = nullptr;
        OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &result);
        if (result)
            CFRelease(result);
        return status == errSecSuccess || status == errSecInteractionNotAllowed;
    }
}

bool Keyring::delete_secret(std::string_view service, std::string_view account)
{
    if (service.empty() || account.empty())
        return false;
    @autoreleasepool {
        NSDictionary* query = @{
            (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
            (__bridge id)kSecAttrService : to_nsstring(service),
            (__bridge id)kSecAttrAccount : to_nsstring(account),
        };
        OSStatus status = SecItemDelete((__bridge CFDictionaryRef)query);
        return status == errSecSuccess || status == errSecItemNotFound;
    }
}

}
