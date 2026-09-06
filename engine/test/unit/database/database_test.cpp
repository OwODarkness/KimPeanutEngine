#include <gtest/gtest.h>

#include "database/database.h"

namespace
{
    using kpengine::database::Database;
    using kpengine::database::DatabaseError;
    using kpengine::database::StatementStep;
    using kpengine::database::Transaction;
}

TEST(DatabaseTest, ExecutesPreparedStatementsAndReadsTypedColumns)
{
    Database database{":memory:"};
    database.Execute("CREATE TABLE records (id INTEGER PRIMARY KEY, name TEXT, weight REAL, payload BLOB);");

    auto insert = database.Prepare("INSERT INTO records (name, weight, payload) VALUES (?, ?, ?);");
    const std::vector<std::byte> payload{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
    insert.Bind(1, "sample");
    insert.Bind(2, 2.5);
    insert.Bind(3, payload);
    EXPECT_EQ(insert.Step(), StatementStep::Done);
    EXPECT_EQ(database.Changes(), 1);
    EXPECT_GT(database.LastInsertRowID(), 0);

    auto query = database.Prepare("SELECT id, name, weight, payload FROM records;");
    ASSERT_EQ(query.Step(), StatementStep::Row);
    EXPECT_EQ(query.ColumnInt64(0), 1);
    EXPECT_EQ(query.ColumnText(1), "sample");
    EXPECT_DOUBLE_EQ(query.ColumnDouble(2), 2.5);
    EXPECT_EQ(query.ColumnBlob(3), payload);
    EXPECT_EQ(query.ColumnCount(), 4);
    EXPECT_EQ(query.Step(), StatementStep::Done);
}

TEST(DatabaseTest, RollsBackUncommittedTransaction)
{
    Database database{":memory:"};
    database.Execute("CREATE TABLE records (value INTEGER);");

    {
        Transaction transaction{database};
        database.Execute("INSERT INTO records VALUES (7);");
    }

    auto query = database.Prepare("SELECT COUNT(*) FROM records;");
    ASSERT_EQ(query.Step(), StatementStep::Row);
    EXPECT_EQ(query.ColumnInt64(0), 0);

    {
        Transaction transaction{database};
        database.Execute("INSERT INTO records VALUES (11);");
        transaction.Commit();
    }

    query.Reset();
    ASSERT_EQ(query.Step(), StatementStep::Row);
    EXPECT_EQ(query.ColumnInt64(0), 1);
}

TEST(DatabaseTest, ReportsSqliteErrorsWithCodes)
{
    Database database{":memory:"};

    try
    {
        database.Execute("INSERT INTO missing_table VALUES (1);");
        FAIL() << "expected SQLite error";
    }
    catch (const DatabaseError &error)
    {
        EXPECT_NE(error.ResultCode(), 0);
        EXPECT_NE(error.ExtendedResultCode(), 0);
        EXPECT_NE(std::string{error.what()}.find("missing_table"), std::string::npos);
    }
}
