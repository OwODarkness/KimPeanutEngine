#ifndef KPENGINE_RUNTIME_CORE_DATABASE_DATABASE_H
#define KPENGINE_RUNTIME_CORE_DATABASE_DATABASE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kpengine::database
{
    enum class DatabaseOpenMode
    {
        ReadWriteCreate,
        ReadOnly,
    };

    class DatabaseError final : public std::runtime_error
    {
    public:
        DatabaseError(std::string message, int result_code, int extended_result_code);

        int ResultCode() const noexcept;
        int ExtendedResultCode() const noexcept;
        bool IsBusy() const noexcept;
        bool IsLocked() const noexcept;

    private:
        int result_code_{};
        int extended_result_code_{};
    };

    enum class StatementStep
    {
        Row,
        Done,
    };

    class Statement final
    {
    public:
        Statement(Statement &&other) noexcept;
        Statement &operator=(Statement &&other) noexcept;
        ~Statement() noexcept;

        Statement(const Statement &) = delete;
        Statement &operator=(const Statement &) = delete;

        void BindNull(int parameter_index);
        void Bind(int parameter_index, int64_t value);
        void Bind(int parameter_index, double value);
        void Bind(int parameter_index, std::string_view value);
        void Bind(int parameter_index, const std::vector<std::byte> &value);

        StatementStep Step();
        void Reset();
        void ClearBindings();

        int ColumnCount() const noexcept;
        bool ColumnIsNull(int column_index) const noexcept;
        int64_t ColumnInt64(int column_index) const noexcept;
        double ColumnDouble(int column_index) const noexcept;
        std::string ColumnText(int column_index) const;
        std::vector<std::byte> ColumnBlob(int column_index) const;

    private:
        struct Impl;

        explicit Statement(std::unique_ptr<Impl> impl) noexcept;

        std::unique_ptr<Impl> impl_{};

        friend class Database;
    };

    class Database final
    {
    public:
        explicit Database(std::string path,
                          DatabaseOpenMode mode = DatabaseOpenMode::ReadWriteCreate);
        Database(Database &&other) noexcept;
        Database &operator=(Database &&other) noexcept;
        ~Database() noexcept;

        Database(const Database &) = delete;
        Database &operator=(const Database &) = delete;

        const std::string &Path() const noexcept;
        void Execute(std::string_view sql);
        Statement Prepare(std::string_view sql);

        void BeginTransaction();
        void Commit();
        void Rollback();

        int64_t LastInsertRowID() const noexcept;
        int Changes() const noexcept;

    private:
        struct Impl;

        void RollbackNoThrow() noexcept;

        std::unique_ptr<Impl> impl_{};

        friend class Transaction;
    };

    // A transaction must not outlive the Database it references. If it is
    // destroyed without Commit or Rollback, it attempts to roll back.
    class Transaction final
    {
    public:
        explicit Transaction(Database &database);
        ~Transaction() noexcept;

        Transaction(const Transaction &) = delete;
        Transaction &operator=(const Transaction &) = delete;
        Transaction(Transaction &&) = delete;
        Transaction &operator=(Transaction &&) = delete;

        void Commit();
        void Rollback();

    private:
        Database *database_{};
        bool finished_{};
    };
}

#endif
