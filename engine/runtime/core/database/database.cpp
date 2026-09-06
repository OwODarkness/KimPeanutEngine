#include "database.h"

#include <limits>
#include <utility>

#include <sqlite/sqlite3.h>

namespace kpengine::database
{
    namespace
    {
        int PrimaryResultCode(int result_code) noexcept
        {
            return result_code & 0xff;
        }

        int CheckedByteCount(std::size_t size)
        {
            if (size > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                throw std::length_error("SQLite value exceeds the supported byte count");
            }
            return static_cast<int>(size);
        }

        std::string SqliteErrorMessage(sqlite3 *database, int result_code,
                                       std::string_view operation)
        {
            const char *detail = database != nullptr ? sqlite3_errmsg(database)
                                                      : sqlite3_errstr(result_code);
            std::string message{operation};
            message += ": ";
            message += detail != nullptr ? detail : "unknown SQLite error";
            return message;
        }

        void CheckResult(sqlite3 *database, int result_code, std::string_view operation)
        {
            if (result_code == SQLITE_OK)
            {
                return;
            }

            const int extended_result_code = database != nullptr
                                                 ? sqlite3_extended_errcode(database)
                                                 : result_code;
            throw DatabaseError(SqliteErrorMessage(database, result_code, operation),
                                result_code, extended_result_code);
        }

        sqlite3 *GetDatabase(sqlite3_stmt *statement) noexcept
        {
            return statement != nullptr ? sqlite3_db_handle(statement) : nullptr;
        }
    }

    DatabaseError::DatabaseError(std::string message, int result_code,
                                 int extended_result_code)
        : std::runtime_error(std::move(message)),
          result_code_(result_code),
          extended_result_code_(extended_result_code)
    {
    }

    int DatabaseError::ResultCode() const noexcept
    {
        return result_code_;
    }

    int DatabaseError::ExtendedResultCode() const noexcept
    {
        return extended_result_code_;
    }

    bool DatabaseError::IsBusy() const noexcept
    {
        return PrimaryResultCode(result_code_) == SQLITE_BUSY ||
               PrimaryResultCode(extended_result_code_) == SQLITE_BUSY;
    }

    bool DatabaseError::IsLocked() const noexcept
    {
        return PrimaryResultCode(result_code_) == SQLITE_LOCKED ||
               PrimaryResultCode(extended_result_code_) == SQLITE_LOCKED;
    }

    struct Statement::Impl
    {
        explicit Impl(sqlite3_stmt *value) noexcept : handle(value) {}

        ~Impl() noexcept
        {
            if (handle != nullptr)
            {
                (void)sqlite3_finalize(handle);
            }
        }

        sqlite3_stmt *handle{};
    };

    Statement::Statement(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
    {
    }

    Statement::Statement(Statement &&other) noexcept = default;

    Statement &Statement::operator=(Statement &&other) noexcept = default;

    Statement::~Statement() noexcept = default;

    void Statement::BindNull(int parameter_index)
    {
        CheckResult(GetDatabase(impl_->handle), sqlite3_bind_null(impl_->handle, parameter_index),
                    "bind null");
    }

    void Statement::Bind(int parameter_index, int64_t value)
    {
        CheckResult(GetDatabase(impl_->handle),
                    sqlite3_bind_int64(impl_->handle, parameter_index, value), "bind integer");
    }

    void Statement::Bind(int parameter_index, double value)
    {
        CheckResult(GetDatabase(impl_->handle),
                    sqlite3_bind_double(impl_->handle, parameter_index, value), "bind double");
    }

    void Statement::Bind(int parameter_index, std::string_view value)
    {
        const int byte_count = CheckedByteCount(value.size());
        CheckResult(GetDatabase(impl_->handle),
                    sqlite3_bind_text(impl_->handle, parameter_index, value.data(), byte_count,
                                      SQLITE_TRANSIENT),
                    "bind text");
    }

    void Statement::Bind(int parameter_index, const std::vector<std::byte> &value)
    {
        const int byte_count = CheckedByteCount(value.size());
        CheckResult(GetDatabase(impl_->handle),
                    sqlite3_bind_blob(impl_->handle, parameter_index, value.data(), byte_count,
                                      SQLITE_TRANSIENT),
                    "bind blob");
    }

    StatementStep Statement::Step()
    {
        const int result_code = sqlite3_step(impl_->handle);
        if (result_code == SQLITE_ROW)
        {
            return StatementStep::Row;
        }
        if (result_code == SQLITE_DONE)
        {
            return StatementStep::Done;
        }

        CheckResult(GetDatabase(impl_->handle), result_code, "step statement");
        return StatementStep::Done;
    }

    void Statement::Reset()
    {
        CheckResult(GetDatabase(impl_->handle), sqlite3_reset(impl_->handle),
                    "reset statement");
    }

    void Statement::ClearBindings()
    {
        CheckResult(GetDatabase(impl_->handle), sqlite3_clear_bindings(impl_->handle),
                    "clear statement bindings");
    }

    int Statement::ColumnCount() const noexcept
    {
        return sqlite3_column_count(impl_->handle);
    }

    bool Statement::ColumnIsNull(int column_index) const noexcept
    {
        return sqlite3_column_type(impl_->handle, column_index) == SQLITE_NULL;
    }

    int64_t Statement::ColumnInt64(int column_index) const noexcept
    {
        return sqlite3_column_int64(impl_->handle, column_index);
    }

    double Statement::ColumnDouble(int column_index) const noexcept
    {
        return sqlite3_column_double(impl_->handle, column_index);
    }

    std::string Statement::ColumnText(int column_index) const
    {
        const unsigned char *const text = sqlite3_column_text(impl_->handle, column_index);
        if (text == nullptr)
        {
            return {};
        }

        const int byte_count = sqlite3_column_bytes(impl_->handle, column_index);
        return {reinterpret_cast<const char *>(text), static_cast<std::size_t>(byte_count)};
    }

    std::vector<std::byte> Statement::ColumnBlob(int column_index) const
    {
        const int byte_count = sqlite3_column_bytes(impl_->handle, column_index);
        const void *const data = sqlite3_column_blob(impl_->handle, column_index);
        if (data == nullptr || byte_count <= 0)
        {
            return {};
        }

        const auto *const bytes = static_cast<const std::byte *>(data);
        return {bytes, bytes + byte_count};
    }

    struct Database::Impl
    {
        ~Impl() noexcept
        {
            if (handle != nullptr)
            {
                (void)sqlite3_close_v2(handle);
            }
        }

        std::string path{};
        sqlite3 *handle{};
    };

    Database::Database(std::string path, DatabaseOpenMode mode)
        : impl_(std::make_unique<Impl>())
    {
        sqlite3 *handle{};
        const int open_flags = mode == DatabaseOpenMode::ReadOnly
                                   ? SQLITE_OPEN_READONLY | SQLITE_OPEN_FULLMUTEX
                                   : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
        const int result_code = sqlite3_open_v2(
            path.c_str(), &handle, open_flags, nullptr);
        if (result_code != SQLITE_OK)
        {
            const std::string message = SqliteErrorMessage(handle, result_code, "open database");
            const int extended_result_code = handle != nullptr
                                                 ? sqlite3_extended_errcode(handle)
                                                 : result_code;
            if (handle != nullptr)
            {
                (void)sqlite3_close_v2(handle);
            }
            throw DatabaseError(message, result_code, extended_result_code);
        }

        impl_->path = std::move(path);
        impl_->handle = handle;
    }

    Database::Database(Database &&other) noexcept = default;

    Database &Database::operator=(Database &&other) noexcept = default;

    Database::~Database() noexcept = default;

    const std::string &Database::Path() const noexcept
    {
        return impl_->path;
    }

    void Database::Execute(std::string_view sql)
    {
        std::string statement{sql};
        char *error_message{};
        const int result_code = sqlite3_exec(impl_->handle, statement.c_str(), nullptr, nullptr,
                                             &error_message);
        if (result_code != SQLITE_OK)
        {
            const std::string message = error_message != nullptr
                                             ? std::string{"execute SQL: "} + error_message
                                             : SqliteErrorMessage(impl_->handle, result_code,
                                                                   "execute SQL");
            if (error_message != nullptr)
            {
                sqlite3_free(error_message);
            }
            throw DatabaseError(message, result_code,
                                sqlite3_extended_errcode(impl_->handle));
        }
    }

    Statement Database::Prepare(std::string_view sql)
    {
        std::string statement{sql};
        sqlite3_stmt *handle{};
        const int result_code = sqlite3_prepare_v2(impl_->handle, statement.c_str(), -1, &handle,
                                                   nullptr);
        if (result_code != SQLITE_OK)
        {
            if (handle != nullptr)
            {
                (void)sqlite3_finalize(handle);
            }
            CheckResult(impl_->handle, result_code, "prepare statement");
        }

        return Statement{std::make_unique<Statement::Impl>(handle)};
    }

    void Database::BeginTransaction()
    {
        Execute("BEGIN TRANSACTION;");
    }

    void Database::Commit()
    {
        Execute("COMMIT;");
    }

    void Database::Rollback()
    {
        Execute("ROLLBACK;");
    }

    int64_t Database::LastInsertRowID() const noexcept
    {
        return sqlite3_last_insert_rowid(impl_->handle);
    }

    int Database::Changes() const noexcept
    {
        return sqlite3_changes(impl_->handle);
    }

    void Database::RollbackNoThrow() noexcept
    {
        try
        {
            Rollback();
        }
        catch (...)
        {
        }
    }

    Transaction::Transaction(Database &database) : database_(&database)
    {
        database_->BeginTransaction();
    }

    Transaction::~Transaction() noexcept
    {
        if (!finished_)
        {
            database_->RollbackNoThrow();
        }
    }

    void Transaction::Commit()
    {
        database_->Commit();
        finished_ = true;
    }

    void Transaction::Rollback()
    {
        database_->Rollback();
        finished_ = true;
    }
}
