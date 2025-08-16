#include <gtest/gtest.h>
#include "Event.hpp"
#include "Error.hpp"
#include "lib/rapidjson/document.h"

using namespace socketio_serverpp::lib;

class EventTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(EventTest, StringEventConstructor) {
    Event event("test_event", "test_data");
    
    EXPECT_EQ(event.name(), "test_event");
    EXPECT_EQ(event.data(), "test_data");
    EXPECT_FALSE(event.isJson());
    EXPECT_EQ(event.size(), 9); // "test_data" length
    EXPECT_FALSE(event.empty());
    EXPECT_EQ(event.json(), nullptr);
}

TEST_F(EventTest, JsonEventConstructor) {
    rapidjson::Document doc;
    doc.Parse<0>("{\"key\": \"value\", \"number\": 42}");
    std::string raw_json = "{\"key\": \"value\", \"number\": 42}";
    
    Event event("json_event", doc, raw_json);
    
    EXPECT_EQ(event.name(), "json_event");
    EXPECT_EQ(event.data(), raw_json);
    EXPECT_TRUE(event.isJson());
    EXPECT_EQ(event.size(), raw_json.size());
    EXPECT_FALSE(event.empty());
    EXPECT_NE(event.json(), nullptr);
}

TEST_F(EventTest, EmptyEventName) {
    EXPECT_THROW({
        Event event("", "data");
    }, SocketIOException);
    
    rapidjson::Document doc;
    doc.Parse<0>("{}");
    EXPECT_THROW({
        Event event("", doc, "{}");
    }, SocketIOException);
}

TEST_F(EventTest, EmptyData) {
    Event event("test", "");
    
    EXPECT_EQ(event.name(), "test");
    EXPECT_TRUE(event.empty());
    EXPECT_EQ(event.size(), 0);
    EXPECT_EQ(event.data(), "");
}

TEST_F(EventTest, JsonDocumentAccess) {
    rapidjson::Document doc;
    doc.Parse<0>("{\"message\": \"hello\", \"count\": 5}");
    std::string raw_json = "{\"message\": \"hello\", \"count\": 5}";
    
    Event event("test", doc, raw_json);
    
    EXPECT_TRUE(event.isJson());
    const rapidjson::Document* json_doc = event.json();
    ASSERT_NE(json_doc, nullptr);
    
    EXPECT_TRUE(json_doc->IsObject());
    EXPECT_TRUE(json_doc->HasMember("message"));
    EXPECT_TRUE(json_doc->HasMember("count"));
    EXPECT_STREQ((*json_doc)["message"].GetString(), "hello");
    EXPECT_EQ((*json_doc)["count"].GetInt(), 5);
}

TEST_F(EventTest, LargeEventData) {
    std::string large_data(10000, 'A');
    Event event("large_event", large_data);
    
    EXPECT_EQ(event.name(), "large_event");
    EXPECT_EQ(event.data(), large_data);
    EXPECT_EQ(event.size(), 10000);
    EXPECT_FALSE(event.empty());
}

TEST_F(EventTest, SpecialCharactersInName) {
    Event event("test_événement_测试", "data");
    
    EXPECT_EQ(event.name(), "test_événement_测试");
    EXPECT_EQ(event.data(), "data");
}

TEST_F(EventTest, SpecialCharactersInData) {
    std::string special_data = "Special chars: \n\r\t\"'\\{}[]";
    Event event("test", special_data);
    
    EXPECT_EQ(event.data(), special_data);
    EXPECT_EQ(event.size(), special_data.size());
}

TEST_F(EventTest, JsonEventWithArray) {
    rapidjson::Document doc;
    doc.Parse<0>("[\"event_name\", {\"data\": \"value\"}, 42]");
    std::string raw_json = "[\"event_name\", {\"data\": \"value\"}, 42]";
    
    Event event("array_event", doc, raw_json);
    
    EXPECT_TRUE(event.isJson());
    const rapidjson::Document* json_doc = event.json();
    ASSERT_NE(json_doc, nullptr);
    
    EXPECT_TRUE(json_doc->IsArray());
    EXPECT_EQ(json_doc->Size(), 3);
    EXPECT_STREQ((*json_doc)[rapidjson::SizeType(0)].GetString(), "event_name");
    EXPECT_EQ((*json_doc)[rapidjson::SizeType(2)].GetInt(), 42);
}

TEST_F(EventTest, InvalidJsonHandling) {
    rapidjson::Document doc;
    doc.Parse<0>("invalid json"); // This will create a parse error
    std::string raw_json = "invalid json";
    
    // Constructor should not throw even with invalid JSON
    // because we pass the raw string
    Event event("test", doc, raw_json);
    
    EXPECT_EQ(event.name(), "test");
    EXPECT_EQ(event.data(), raw_json);
    EXPECT_TRUE(event.isJson()); // Still marked as JSON event
}

TEST_F(EventTest, CopyBehavior) {
    rapidjson::Document doc;
    doc.Parse<0>("{\"test\": true}");
    std::string raw_json = "{\"test\": true}";
    
    Event original("original", doc, raw_json);
    Event copied = original;
    
    EXPECT_EQ(copied.name(), original.name());
    EXPECT_EQ(copied.data(), original.data());
    EXPECT_EQ(copied.isJson(), original.isJson());
    EXPECT_EQ(copied.size(), original.size());
    EXPECT_EQ(copied.empty(), original.empty());
    
    // JSON pointer should be the same (shallow copy)
    EXPECT_EQ(copied.json(), original.json());
}

TEST_F(EventTest, EventNameValidation) {
    // Valid event names
    EXPECT_NO_THROW(Event("a", "data"));
    EXPECT_NO_THROW(Event("event", "data"));
    EXPECT_NO_THROW(Event("snake_case", "data"));
    EXPECT_NO_THROW(Event("camelCase", "data"));
    EXPECT_NO_THROW(Event("kebab-case", "data"));
    EXPECT_NO_THROW(Event("with.dots", "data"));
    EXPECT_NO_THROW(Event("with123numbers", "data"));
    
    // Invalid event names
    EXPECT_THROW(Event("", "data"), SocketIOException);
}

TEST_F(EventTest, DataTypePersistence) {
    // String event
    Event string_event("test", "string_data");
    EXPECT_FALSE(string_event.isJson());
    EXPECT_EQ(string_event.json(), nullptr);
    
    // JSON event
    rapidjson::Document doc;
    doc.Parse<0>("{}");
    Event json_event("test", doc, "{}");
    EXPECT_TRUE(json_event.isJson());
    EXPECT_NE(json_event.json(), nullptr);
}
