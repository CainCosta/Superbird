/*
 * Copyright (c) 2026, Superbird contributors.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <UI/Qt/Superbird/Modules/Passwords/Core/BitwardenCrypto.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/OnePasswordParser.h>
#include <UI/Qt/Superbird/Modules/Passwords/Core/UriMatcher.h>
#include <UI/Qt/Superbird/Modules/Passwords/Providers/OnePasswordProvider.h>

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <chrono>

namespace Superbird::Passwords {

namespace {

int64_t now_epoch()
{
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch())
        .count();
}

QString to_qstr(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}

} // namespace

OnePasswordProvider::OnePasswordProvider(QObject* parent)
    : QObject(parent)
{
    m_state = is_cli_available() ? ProviderState::Locked : ProviderState::Unconfigured;
}

OnePasswordProvider::~OnePasswordProvider()
{
    m_cached_summaries.clear();
}

QString OnePasswordProvider::find_op_binary()
{
    if (auto found = QStandardPaths::findExecutable("op"); !found.isEmpty())
        return found;
    static QStringList const fallback_paths = {
        "/opt/homebrew/bin/op",
        "/usr/local/bin/op",
        "/usr/bin/op",
    };
    for (auto const& path : fallback_paths) {
        QFileInfo info(path);
        if (info.exists() && info.isExecutable())
            return path;
    }
    return {};
}

bool OnePasswordProvider::is_cli_available()
{
    return !find_op_binary().isEmpty();
}

std::string OnePasswordProvider::setup_guidance()
{
    return "1. Install the official 1Password CLI (`op` v2) — e.g. `brew install 1password-cli` on macOS or via your Linux package manager.\n"
           "2. Open 1Password Desktop → Settings → Developer → enable \"Integrate with 1Password CLI\".";
}

ProviderCapabilities OnePasswordProvider::capabilities() const
{
    return {
        .can_sync = true,
        .can_create = true,
        .can_update = true,
        .supports_biometric_unlock = true,
        .supports_totp = true,
    };
}

void OnePasswordProvider::run_op_async(
    QStringList const& args,
    QByteArray const& stdin_data,
    std::function<void(int exit_code, std::string stdout_str, std::string stderr_str)> callback)
{
    auto op_bin = find_op_binary();
    if (op_bin.isEmpty()) {
        if (callback)
            callback(-1, {}, "1Password CLI (`op`) not found. " + setup_guidance());
        return;
    }

    auto* proc = new QProcess(this);
    proc->setProgram(op_bin);
    proc->setArguments(args);

    connect(proc, &QProcess::finished, this, [proc, callback = std::move(callback)](int exit_code, QProcess::ExitStatus exit_status) {
        auto out_bytes = proc->readAllStandardOutput();
        auto err_bytes = proc->readAllStandardError();
        proc->deleteLater();
        std::string out_str(out_bytes.constData(), static_cast<size_t>(out_bytes.size()));
        std::string err_str(err_bytes.constData(), static_cast<size_t>(err_bytes.size()));
        if (exit_status != QProcess::NormalExit)
            exit_code = -1;
        if (callback)
            callback(exit_code, std::move(out_str), std::move(err_str));
    });

    connect(proc, &QProcess::errorOccurred, this, [proc, callback](QProcess::ProcessError) {
        if (proc->state() == QProcess::NotRunning) {
            auto err = proc->errorString().toStdString();
            proc->deleteLater();
            if (callback)
                callback(-1, {}, err);
        }
    });

    proc->start();
    if (!stdin_data.isEmpty()) {
        proc->write(stdin_data);
        proc->closeWriteChannel();
    }
}

void OnePasswordProvider::unlock(UnlockRequest const&, StatusCallback callback)
{
    if (!is_cli_available()) {
        m_state = ProviderState::Unconfigured;
        m_last_error = "1Password CLI (`op`) not installed.";
        if (on_changed)
            on_changed();
        if (callback)
            callback(false, m_last_error);
        return;
    }

    m_state = ProviderState::Unlocking;
    if (on_changed)
        on_changed();

    sync(std::move(callback));
}

void OnePasswordProvider::sync(StatusCallback callback)
{
    if (!is_cli_available()) {
        m_state = ProviderState::Unconfigured;
        if (callback)
            callback(false, "1Password CLI (`op`) not installed.");
        return;
    }

    run_op_async({ "item", "list", "--categories", "Login", "--format", "json" }, {}, [this, callback = std::move(callback)](int exit_code, std::string out, std::string err) {
        if (exit_code != 0) {
            m_state = ProviderState::Locked;
            m_last_error = err.empty() ? "1Password unlock failed. Enable Desktop App Integration in 1Password Settings → Developer." : err;
            if (on_changed)
                on_changed();
            if (callback)
                callback(false, m_last_error);
            return;
        }

        auto parsed = OnePasswordParser::parse_item_list(out, account_id());
        if (!parsed) {
            m_state = ProviderState::Error;
            m_last_error = "Failed to parse 1Password item list JSON.";
            if (on_changed)
                on_changed();
            if (callback)
                callback(false, m_last_error);
            return;
        }

        m_cached_summaries = std::move(*parsed);
        m_last_sync_time = now_epoch();
        m_state = ProviderState::Unlocked;
        m_last_error.clear();
        if (on_changed)
            on_changed();
        if (callback)
            callback(true, {});
    });
}

void OnePasswordProvider::lock()
{
    m_cached_summaries.clear();
    m_state = is_cli_available() ? ProviderState::Locked : ProviderState::Unconfigured;
    m_last_error.clear();
    if (on_changed)
        on_changed();
}

std::vector<CredentialSummary> OnePasswordProvider::all_credentials() const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return m_cached_summaries;
}

std::vector<CredentialSummary> OnePasswordProvider::match_for_url(std::string_view url) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::filter_and_rank(url, m_cached_summaries);
}

std::vector<CredentialSummary> OnePasswordProvider::search(std::string_view query) const
{
    if (m_state != ProviderState::Unlocked)
        return {};
    return UriMatcher::search(query, m_cached_summaries);
}

void OnePasswordProvider::reveal_secret(std::string_view id, SecretCallback callback)
{
    if (!callback)
        return;
    if (m_state != ProviderState::Unlocked) {
        callback(std::nullopt, "1Password is locked.");
        return;
    }

    run_op_async({ "item", "get", to_qstr(id), "--format", "json", "--reveal" }, {}, [callback = std::move(callback)](int exit_code, std::string out, std::string err) {
        if (exit_code != 0) {
            cleanse_string(out);
            callback(std::nullopt, err.empty() ? "1Password failed to reveal item." : err);
            return;
        }
        auto secret = OnePasswordParser::parse_item_detail(out);
        cleanse_string(out);
        if (!secret) {
            callback(std::nullopt, "Failed to parse revealed 1Password item.");
            return;
        }
        callback(std::move(secret), {});
    });
}

void OnePasswordProvider::fetch_otp(std::string_view id, std::function<void(std::optional<std::string> otp, std::string error)> callback)
{
    if (!callback)
        return;
    run_op_async({ "item", "get", to_qstr(id), "--otp" }, {}, [callback = std::move(callback)](int exit_code, std::string out, std::string err) {
        if (exit_code != 0) {
            callback(std::nullopt, err.empty() ? "No one-time password available." : err);
            return;
        }
        auto otp = OnePasswordParser::parse_otp_output(out);
        cleanse_string(out);
        if (!otp) {
            callback(std::nullopt, "Invalid OTP response from 1Password.");
            return;
        }
        callback(std::move(otp), {});
    });
}

void OnePasswordProvider::create_login(NewLogin const& login, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked) {
        if (callback)
            callback(std::nullopt, "1Password is locked.");
        return;
    }

    // Pass login JSON template on stdin so secrets are never exposed on the process command line.
    QJsonObject doc;
    doc["title"] = to_qstr(login.name.empty() ? login.username : login.name);
    doc["category"] = "LOGIN";

    QJsonArray fields;
    {
        QJsonObject u;
        u["id"] = "username";
        u["type"] = "STRING";
        u["purpose"] = "USERNAME";
        u["value"] = to_qstr(login.username);
        fields.append(u);
    }
    {
        QJsonObject p;
        p["id"] = "password";
        p["type"] = "CONCEALED";
        p["purpose"] = "PASSWORD";
        p["value"] = to_qstr(login.password);
        fields.append(p);
    }
    doc["fields"] = fields;

    if (!login.uris.empty()) {
        QJsonArray urls;
        for (size_t i = 0; i < login.uris.size(); ++i) {
            QJsonObject uo;
            uo["primary"] = (i == 0);
            uo["href"] = to_qstr(login.uris[i].uri);
            urls.append(uo);
        }
        doc["urls"] = urls;
    }

    QByteArray payload = QJsonDocument(doc).toJson(QJsonDocument::Compact);
    run_op_async({ "item", "create", "--format", "json", "-" }, payload, [this, login, callback = std::move(callback)](int exit_code, std::string out, std::string err) {
        if (exit_code != 0) {
            cleanse_string(out);
            if (callback)
                callback(std::nullopt, err.empty() ? "Failed to save login to 1Password." : err);
            return;
        }
        auto obj = QJsonDocument::fromJson(QByteArray::fromStdString(out)).object();
        cleanse_string(out);
        CredentialSummary summary;
        summary.provider = ProviderKind::OnePassword;
        summary.provider_account_id = account_id();
        summary.id = obj["id"].toString().toStdString();
        summary.name = obj["title"].toString().toStdString();
        summary.username = login.username;
        summary.uris = login.uris;
        summary.has_password = !login.password.empty();
        if (!summary.id.empty())
            m_cached_summaries.push_back(summary);
        if (on_changed)
            on_changed();
        if (callback)
            callback(std::move(summary), {});
    });
}

void OnePasswordProvider::update_login(std::string_view id, UpdatedLogin const& update, WriteCallback callback)
{
    if (m_state != ProviderState::Unlocked) {
        if (callback)
            callback(std::nullopt, "1Password is locked.");
        return;
    }

    // Fetch current item JSON, merge changes, and pipe back via stdin (`op item edit <id> --format json -`).
    std::string id_str(id);
    run_op_async({ "item", "get", to_qstr(id_str), "--format", "json", "--reveal" }, {}, [this, id_str, update, callback = std::move(callback)](int exit_code, std::string out, std::string err) {
        if (exit_code != 0) {
            cleanse_string(out);
            if (callback)
                callback(std::nullopt, err.empty() ? "Failed to load 1Password item for update." : err);
            return;
        }

        auto obj = QJsonDocument::fromJson(QByteArray::fromStdString(out)).object();
        cleanse_string(out);
        if (update.name.has_value())
            obj["title"] = to_qstr(*update.name);

        auto fields = obj["fields"].toArray();
        for (int i = 0; i < fields.size(); ++i) {
            auto f = fields[i].toObject();
            auto purpose = f["purpose"].toString().toUpper();
            if (purpose == "USERNAME" && update.username.has_value())
                f["value"] = to_qstr(*update.username);
            else if (purpose == "PASSWORD" && update.password.has_value())
                f["value"] = to_qstr(*update.password);
            fields[i] = f;
        }
        obj["fields"] = fields;

        QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        run_op_async({ "item", "edit", to_qstr(id_str), "--format", "json", "-" }, payload, [this, id_str, update, callback = std::move(callback)](int edit_code, std::string edit_out, std::string edit_err) {
            cleanse_string(edit_out);
            if (edit_code != 0) {
                if (callback)
                    callback(std::nullopt, edit_err.empty() ? "Failed to update 1Password item." : edit_err);
                return;
            }
            std::optional<CredentialSummary> updated_summary;
            for (auto& s : m_cached_summaries) {
                if (s.id == id_str) {
                    if (update.name.has_value())
                        s.name = *update.name;
                    if (update.username.has_value())
                        s.username = *update.username;
                    updated_summary = s;
                    break;
                }
            }
            if (on_changed)
                on_changed();
            if (callback)
                callback(std::move(updated_summary), {});
        });
    });
}

}
