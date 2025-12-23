// BSD 3-Clause License
//
// Copyright (c) 2025, kcenon
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
//    this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and/or other materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its
//    contributors may be used to endorse or promote products derived from
//    this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/**
 * @file query_protocol_test.cpp
 * @brief Unit tests for query protocol message structures (Phase 3.1)
 *
 * Tests cover:
 * - Query type and status code conversions
 * - Auth token validation
 * - Query request creation and validation
 * - Query response creation
 * - Serialization/deserialization round-trips (when container_system available)
 */

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <kcenon/database_server/gateway/query_protocol.h>
#include <kcenon/database_server/gateway/query_types.h>

#include <kcenon/common/config/feature_flags.h>

using namespace database_server::gateway;

// ============================================================================
// Query Types Tests
// ============================================================================

class QueryTypesTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(QueryTypesTest, QueryTypeToString)
{
	EXPECT_EQ(to_string(query_type::select), "SELECT");
	EXPECT_EQ(to_string(query_type::insert), "INSERT");
	EXPECT_EQ(to_string(query_type::update), "UPDATE");
	EXPECT_EQ(to_string(query_type::del), "DELETE");
	EXPECT_EQ(to_string(query_type::execute), "EXECUTE");
	EXPECT_EQ(to_string(query_type::batch), "BATCH");
	EXPECT_EQ(to_string(query_type::ping), "PING");
	EXPECT_EQ(to_string(query_type::unknown), "UNKNOWN");
}

TEST_F(QueryTypesTest, StatusCodeToString)
{
	EXPECT_EQ(to_string(status_code::ok), "OK");
	EXPECT_EQ(to_string(status_code::error), "ERROR");
	EXPECT_EQ(to_string(status_code::timeout), "TIMEOUT");
	EXPECT_EQ(to_string(status_code::connection_failed), "CONNECTION_FAILED");
	EXPECT_EQ(to_string(status_code::authentication_failed), "AUTHENTICATION_FAILED");
	EXPECT_EQ(to_string(status_code::invalid_query), "INVALID_QUERY");
	EXPECT_EQ(to_string(status_code::no_connection), "NO_CONNECTION");
	EXPECT_EQ(to_string(status_code::rate_limited), "RATE_LIMITED");
	EXPECT_EQ(to_string(status_code::server_busy), "SERVER_BUSY");
	EXPECT_EQ(to_string(status_code::not_found), "NOT_FOUND");
	EXPECT_EQ(to_string(status_code::permission_denied), "PERMISSION_DENIED");
}

TEST_F(QueryTypesTest, ParseQueryTypeUppercase)
{
	EXPECT_EQ(parse_query_type("SELECT"), query_type::select);
	EXPECT_EQ(parse_query_type("INSERT"), query_type::insert);
	EXPECT_EQ(parse_query_type("UPDATE"), query_type::update);
	EXPECT_EQ(parse_query_type("DELETE"), query_type::del);
	EXPECT_EQ(parse_query_type("EXECUTE"), query_type::execute);
	EXPECT_EQ(parse_query_type("BATCH"), query_type::batch);
	EXPECT_EQ(parse_query_type("PING"), query_type::ping);
}

TEST_F(QueryTypesTest, ParseQueryTypeLowercase)
{
	EXPECT_EQ(parse_query_type("select"), query_type::select);
	EXPECT_EQ(parse_query_type("insert"), query_type::insert);
	EXPECT_EQ(parse_query_type("update"), query_type::update);
	EXPECT_EQ(parse_query_type("delete"), query_type::del);
	EXPECT_EQ(parse_query_type("execute"), query_type::execute);
	EXPECT_EQ(parse_query_type("batch"), query_type::batch);
	EXPECT_EQ(parse_query_type("ping"), query_type::ping);
}

TEST_F(QueryTypesTest, ParseQueryTypeUnknown)
{
	EXPECT_EQ(parse_query_type("INVALID"), query_type::unknown);
	EXPECT_EQ(parse_query_type(""), query_type::unknown);
	EXPECT_EQ(parse_query_type("Select"), query_type::unknown);
	EXPECT_EQ(parse_query_type("DROP"), query_type::unknown);
}

// ============================================================================
// Message Header Tests
// ============================================================================

class MessageHeaderTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(MessageHeaderTest, DefaultConstruction)
{
	message_header header;

	EXPECT_EQ(header.version, 1);
	EXPECT_EQ(header.message_id, 0);
	EXPECT_EQ(header.timestamp, 0);
	EXPECT_TRUE(header.correlation_id.empty());
}

TEST_F(MessageHeaderTest, ConstructionWithId)
{
	message_header header(12345);

	EXPECT_EQ(header.version, 1);
	EXPECT_EQ(header.message_id, 12345);
	EXPECT_GT(header.timestamp, 0);
}

TEST_F(MessageHeaderTest, TimestampIsReasonable)
{
	auto before = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());

	message_header header(1);

	auto after = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count());

	EXPECT_GE(header.timestamp, before);
	EXPECT_LE(header.timestamp, after);
}

// ============================================================================
// Auth Token Tests
// ============================================================================

class AuthTokenTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(AuthTokenTest, EmptyTokenIsInvalid)
{
	auth_token token;

	EXPECT_TRUE(token.token.empty());
	EXPECT_FALSE(token.is_valid());
}

TEST_F(AuthTokenTest, ValidTokenWithNoExpiry)
{
	auth_token token;
	token.token = "test-token-12345";
	token.client_id = "client-001";
	token.expires_at = 0;

	EXPECT_TRUE(token.is_valid());
	EXPECT_FALSE(token.is_expired());
}

TEST_F(AuthTokenTest, ValidTokenWithFutureExpiry)
{
	auth_token token;
	token.token = "test-token-12345";
	token.client_id = "client-001";

	auto future = std::chrono::system_clock::now() + std::chrono::hours(1);
	token.expires_at = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(future.time_since_epoch())
			.count());

	EXPECT_TRUE(token.is_valid());
	EXPECT_FALSE(token.is_expired());
}

TEST_F(AuthTokenTest, ExpiredToken)
{
	auth_token token;
	token.token = "test-token-12345";
	token.client_id = "client-001";

	auto past = std::chrono::system_clock::now() - std::chrono::hours(1);
	token.expires_at = static_cast<uint64_t>(
		std::chrono::duration_cast<std::chrono::milliseconds>(past.time_since_epoch())
			.count());

	EXPECT_TRUE(token.is_expired());
	EXPECT_FALSE(token.is_valid());
}

// ============================================================================
// Query Param Tests
// ============================================================================

class QueryParamTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(QueryParamTest, DefaultConstruction)
{
	query_param param;

	EXPECT_TRUE(param.name.empty());
	EXPECT_TRUE(std::holds_alternative<std::monostate>(param.value));
}

TEST_F(QueryParamTest, ConstructWithStringValue)
{
	query_param param("name", std::string("John"));

	EXPECT_EQ(param.name, "name");
	EXPECT_TRUE(std::holds_alternative<std::string>(param.value));
	EXPECT_EQ(std::get<std::string>(param.value), "John");
}

TEST_F(QueryParamTest, ConstructWithIntValue)
{
	query_param param("age", static_cast<int64_t>(25));

	EXPECT_EQ(param.name, "age");
	EXPECT_TRUE(std::holds_alternative<int64_t>(param.value));
	EXPECT_EQ(std::get<int64_t>(param.value), 25);
}

TEST_F(QueryParamTest, ConstructWithDoubleValue)
{
	query_param param("price", 19.99);

	EXPECT_EQ(param.name, "price");
	EXPECT_TRUE(std::holds_alternative<double>(param.value));
	EXPECT_DOUBLE_EQ(std::get<double>(param.value), 19.99);
}

TEST_F(QueryParamTest, ConstructWithBoolValue)
{
	query_param param("active", true);

	EXPECT_EQ(param.name, "active");
	EXPECT_TRUE(std::holds_alternative<bool>(param.value));
	EXPECT_TRUE(std::get<bool>(param.value));
}

TEST_F(QueryParamTest, ConstructWithBinaryValue)
{
	std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x04 };
	query_param param("data", data);

	EXPECT_EQ(param.name, "data");
	EXPECT_TRUE(std::holds_alternative<std::vector<uint8_t>>(param.value));
	EXPECT_EQ(std::get<std::vector<uint8_t>>(param.value), data);
}

// ============================================================================
// Query Request Tests
// ============================================================================

class QueryRequestTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(QueryRequestTest, DefaultConstruction)
{
	query_request request;

	EXPECT_EQ(request.type, query_type::unknown);
	EXPECT_TRUE(request.sql.empty());
	EXPECT_TRUE(request.params.empty());
}

TEST_F(QueryRequestTest, ConstructWithSqlAndType)
{
	query_request request("SELECT * FROM users", query_type::select);

	EXPECT_EQ(request.sql, "SELECT * FROM users");
	EXPECT_EQ(request.type, query_type::select);
}

TEST_F(QueryRequestTest, IsValidWithValidRequest)
{
	query_request request("SELECT * FROM users", query_type::select);

	EXPECT_TRUE(request.is_valid());
}

TEST_F(QueryRequestTest, IsValidWithUnknownType)
{
	query_request request("SELECT * FROM users", query_type::unknown);

	EXPECT_FALSE(request.is_valid());
}

TEST_F(QueryRequestTest, IsValidWithEmptySql)
{
	query_request request("", query_type::select);

	EXPECT_FALSE(request.is_valid());
}

TEST_F(QueryRequestTest, IsValidPingWithEmptySql)
{
	query_request request("", query_type::ping);

	EXPECT_TRUE(request.is_valid());
}

TEST_F(QueryRequestTest, DefaultOptions)
{
	query_request request;

	EXPECT_EQ(request.options.timeout_ms, 30000);
	EXPECT_FALSE(request.options.read_only);
	EXPECT_TRUE(request.options.isolation_level.empty());
	EXPECT_EQ(request.options.max_rows, 0);
	EXPECT_TRUE(request.options.include_metadata);
}

TEST_F(QueryRequestTest, AddParameters)
{
	query_request request("SELECT * FROM users WHERE id = ? AND active = ?", query_type::select);
	request.params.emplace_back("id", static_cast<int64_t>(123));
	request.params.emplace_back("active", true);

	EXPECT_EQ(request.params.size(), 2);
	EXPECT_EQ(request.params[0].name, "id");
	EXPECT_EQ(std::get<int64_t>(request.params[0].value), 123);
	EXPECT_EQ(request.params[1].name, "active");
	EXPECT_TRUE(std::get<bool>(request.params[1].value));
}

#if KCENON_WITH_CONTAINER_SYSTEM

TEST_F(QueryRequestTest, SerializeNotNull)
{
	query_request request("SELECT 1", query_type::select);
	auto container = request.serialize();

	EXPECT_NE(container, nullptr);
}

TEST_F(QueryRequestTest, SerializeDeserializeRoundTrip)
{
	query_request original("SELECT * FROM users WHERE id = ?", query_type::select);
	original.header.message_id = 12345;
	original.header.correlation_id = "corr-123";
	original.token.token = "auth-token";
	original.token.client_id = "client-001";
	original.options.timeout_ms = 5000;
	original.options.read_only = true;
	original.params.emplace_back("id", static_cast<int64_t>(42));

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_request::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	EXPECT_EQ(deserialized.header.message_id, original.header.message_id);
	EXPECT_EQ(deserialized.header.correlation_id, original.header.correlation_id);
	EXPECT_EQ(deserialized.token.token, original.token.token);
	EXPECT_EQ(deserialized.token.client_id, original.token.client_id);
	EXPECT_EQ(deserialized.type, original.type);
	EXPECT_EQ(deserialized.sql, original.sql);
	EXPECT_EQ(deserialized.options.timeout_ms, original.options.timeout_ms);
	EXPECT_EQ(deserialized.options.read_only, original.options.read_only);
	ASSERT_EQ(deserialized.params.size(), original.params.size());
	EXPECT_EQ(deserialized.params[0].name, original.params[0].name);
	EXPECT_EQ(std::get<int64_t>(deserialized.params[0].value),
			  std::get<int64_t>(original.params[0].value));
}

TEST_F(QueryRequestTest, DeserializeNullContainer)
{
	auto result = query_request::deserialize(nullptr);

	EXPECT_TRUE(result.is_err());
}

TEST_F(QueryRequestTest, SerializeWithAllParamTypes)
{
	query_request original("INSERT INTO test VALUES (?, ?, ?, ?, ?, ?)", query_type::insert);
	original.params.emplace_back("null_val", std::monostate{});
	original.params.emplace_back("bool_val", true);
	original.params.emplace_back("int_val", static_cast<int64_t>(123));
	original.params.emplace_back("double_val", 3.14);
	original.params.emplace_back("string_val", std::string("hello"));
	original.params.emplace_back("bytes_val", std::vector<uint8_t>{ 0x01, 0x02, 0x03 });

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_request::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	ASSERT_EQ(deserialized.params.size(), 6);

	EXPECT_TRUE(std::holds_alternative<std::monostate>(deserialized.params[0].value));
	EXPECT_TRUE(std::get<bool>(deserialized.params[1].value));
	EXPECT_EQ(std::get<int64_t>(deserialized.params[2].value), 123);
	EXPECT_DOUBLE_EQ(std::get<double>(deserialized.params[3].value), 3.14);
	EXPECT_EQ(std::get<std::string>(deserialized.params[4].value), "hello");
	EXPECT_EQ(std::get<std::vector<uint8_t>>(deserialized.params[5].value),
			  (std::vector<uint8_t>{ 0x01, 0x02, 0x03 }));
}

#endif // KCENON_WITH_CONTAINER_SYSTEM

// ============================================================================
// Query Response Tests
// ============================================================================

class QueryResponseTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(QueryResponseTest, DefaultConstruction)
{
	query_response response;

	EXPECT_EQ(response.status, status_code::ok);
	EXPECT_TRUE(response.columns.empty());
	EXPECT_TRUE(response.rows.empty());
	EXPECT_EQ(response.affected_rows, 0);
	EXPECT_TRUE(response.error_message.empty());
}

TEST_F(QueryResponseTest, ConstructSuccessResponse)
{
	query_response response(12345);

	EXPECT_EQ(response.header.message_id, 12345);
	EXPECT_EQ(response.status, status_code::ok);
	EXPECT_TRUE(response.is_success());
	EXPECT_GT(response.header.timestamp, 0);
}

TEST_F(QueryResponseTest, ConstructErrorResponse)
{
	query_response response(12345, status_code::invalid_query, "SQL syntax error");

	EXPECT_EQ(response.header.message_id, 12345);
	EXPECT_EQ(response.status, status_code::invalid_query);
	EXPECT_EQ(response.error_message, "SQL syntax error");
	EXPECT_FALSE(response.is_success());
}

TEST_F(QueryResponseTest, IsSuccessForAllStatusCodes)
{
	EXPECT_TRUE(query_response(1, status_code::ok, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::error, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::timeout, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::connection_failed, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::authentication_failed, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::invalid_query, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::no_connection, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::rate_limited, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::server_busy, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::not_found, "").is_success());
	EXPECT_FALSE(query_response(1, status_code::permission_denied, "").is_success());
}

TEST_F(QueryResponseTest, AddColumnMetadata)
{
	query_response response(1);

	column_metadata col1;
	col1.name = "id";
	col1.type_name = "INTEGER";
	col1.type_id = 1;
	col1.nullable = false;

	column_metadata col2;
	col2.name = "name";
	col2.type_name = "VARCHAR";
	col2.type_id = 2;
	col2.nullable = true;
	col2.precision = 255;

	response.columns.push_back(col1);
	response.columns.push_back(col2);

	EXPECT_EQ(response.columns.size(), 2);
	EXPECT_EQ(response.columns[0].name, "id");
	EXPECT_EQ(response.columns[1].name, "name");
}

TEST_F(QueryResponseTest, AddResultRows)
{
	query_response response(1);

	result_row row1;
	row1.cells.emplace_back(static_cast<int64_t>(1));
	row1.cells.emplace_back(std::string("Alice"));

	result_row row2;
	row2.cells.emplace_back(static_cast<int64_t>(2));
	row2.cells.emplace_back(std::string("Bob"));

	response.rows.push_back(row1);
	response.rows.push_back(row2);

	EXPECT_EQ(response.rows.size(), 2);
	EXPECT_EQ(std::get<int64_t>(response.rows[0].cells[0]), 1);
	EXPECT_EQ(std::get<std::string>(response.rows[0].cells[1]), "Alice");
	EXPECT_EQ(std::get<int64_t>(response.rows[1].cells[0]), 2);
	EXPECT_EQ(std::get<std::string>(response.rows[1].cells[1]), "Bob");
}

#if KCENON_WITH_CONTAINER_SYSTEM

TEST_F(QueryResponseTest, SerializeNotNull)
{
	query_response response(1);
	auto container = response.serialize();

	EXPECT_NE(container, nullptr);
}

TEST_F(QueryResponseTest, SerializeDeserializeRoundTrip)
{
	query_response original(12345);
	original.header.correlation_id = "corr-456";
	original.affected_rows = 10;
	original.execution_time_us = 5000;

	column_metadata col;
	col.name = "id";
	col.type_name = "INTEGER";
	col.type_id = 1;
	col.nullable = false;
	original.columns.push_back(col);

	result_row row;
	row.cells.emplace_back(static_cast<int64_t>(42));
	original.rows.push_back(row);

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_response::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	EXPECT_EQ(deserialized.header.message_id, original.header.message_id);
	EXPECT_EQ(deserialized.header.correlation_id, original.header.correlation_id);
	EXPECT_EQ(deserialized.status, original.status);
	EXPECT_EQ(deserialized.affected_rows, original.affected_rows);
	EXPECT_EQ(deserialized.execution_time_us, original.execution_time_us);
	ASSERT_EQ(deserialized.columns.size(), 1);
	EXPECT_EQ(deserialized.columns[0].name, "id");
	ASSERT_EQ(deserialized.rows.size(), 1);
	EXPECT_EQ(std::get<int64_t>(deserialized.rows[0].cells[0]), 42);
}

TEST_F(QueryResponseTest, DeserializeNullContainer)
{
	auto result = query_response::deserialize(nullptr);

	EXPECT_TRUE(result.is_err());
}

TEST_F(QueryResponseTest, SerializeErrorResponse)
{
	query_response original(999, status_code::timeout, "Query timed out after 30 seconds");

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_response::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	EXPECT_EQ(deserialized.status, status_code::timeout);
	EXPECT_EQ(deserialized.error_message, "Query timed out after 30 seconds");
	EXPECT_FALSE(deserialized.is_success());
}

TEST_F(QueryResponseTest, SerializeWithAllCellTypes)
{
	query_response original(1);

	for (int i = 0; i < 6; ++i)
	{
		column_metadata col;
		col.name = "col_" + std::to_string(i);
		col.type_name = "VARIANT";
		original.columns.push_back(col);
	}

	result_row row;
	row.cells.emplace_back(std::monostate{});
	row.cells.emplace_back(true);
	row.cells.emplace_back(static_cast<int64_t>(123));
	row.cells.emplace_back(3.14);
	row.cells.emplace_back(std::string("hello"));
	row.cells.emplace_back(std::vector<uint8_t>{ 0xDE, 0xAD, 0xBE, 0xEF });
	original.rows.push_back(row);

	auto container = original.serialize();
	ASSERT_NE(container, nullptr);

	auto result = query_response::deserialize(container);
	ASSERT_TRUE(result.is_ok());

	const auto& deserialized = result.value();
	ASSERT_EQ(deserialized.rows.size(), 1);
	ASSERT_EQ(deserialized.rows[0].cells.size(), 6);

	EXPECT_TRUE(std::holds_alternative<std::monostate>(deserialized.rows[0].cells[0]));
	EXPECT_TRUE(std::get<bool>(deserialized.rows[0].cells[1]));
	EXPECT_EQ(std::get<int64_t>(deserialized.rows[0].cells[2]), 123);
	EXPECT_DOUBLE_EQ(std::get<double>(deserialized.rows[0].cells[3]), 3.14);
	EXPECT_EQ(std::get<std::string>(deserialized.rows[0].cells[4]), "hello");
	EXPECT_EQ(std::get<std::vector<uint8_t>>(deserialized.rows[0].cells[5]),
			  (std::vector<uint8_t>{ 0xDE, 0xAD, 0xBE, 0xEF }));
}

#endif // KCENON_WITH_CONTAINER_SYSTEM

// ============================================================================
// Query Options Tests
// ============================================================================

class QueryOptionsTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(QueryOptionsTest, DefaultValues)
{
	query_options options;

	EXPECT_EQ(options.timeout_ms, 30000);
	EXPECT_FALSE(options.read_only);
	EXPECT_TRUE(options.isolation_level.empty());
	EXPECT_EQ(options.max_rows, 0);
	EXPECT_TRUE(options.include_metadata);
}

TEST_F(QueryOptionsTest, CustomValues)
{
	query_options options;
	options.timeout_ms = 5000;
	options.read_only = true;
	options.isolation_level = "READ_COMMITTED";
	options.max_rows = 100;
	options.include_metadata = false;

	EXPECT_EQ(options.timeout_ms, 5000);
	EXPECT_TRUE(options.read_only);
	EXPECT_EQ(options.isolation_level, "READ_COMMITTED");
	EXPECT_EQ(options.max_rows, 100);
	EXPECT_FALSE(options.include_metadata);
}

// ============================================================================
// Column Metadata Tests
// ============================================================================

class ColumnMetadataTest : public ::testing::Test
{
protected:
	void SetUp() override {}
	void TearDown() override {}
};

TEST_F(ColumnMetadataTest, DefaultValues)
{
	column_metadata col;

	EXPECT_TRUE(col.name.empty());
	EXPECT_TRUE(col.type_name.empty());
	EXPECT_EQ(col.type_id, 0);
	EXPECT_TRUE(col.nullable);
	EXPECT_EQ(col.precision, 0);
	EXPECT_EQ(col.scale, 0);
}

TEST_F(ColumnMetadataTest, NumericColumn)
{
	column_metadata col;
	col.name = "price";
	col.type_name = "DECIMAL";
	col.type_id = 3;
	col.nullable = false;
	col.precision = 10;
	col.scale = 2;

	EXPECT_EQ(col.name, "price");
	EXPECT_EQ(col.type_name, "DECIMAL");
	EXPECT_EQ(col.precision, 10);
	EXPECT_EQ(col.scale, 2);
}
