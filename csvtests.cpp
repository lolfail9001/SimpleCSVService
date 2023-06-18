#include <gtest/gtest.h>
#include "csvloadapp.hpp"

TEST(JSONPackTest, EmptyJSONPack) {
  EXPECT_EQ(std::string {"{}"},
	    fiasco::PackJSON(fiasco::JSONData()));
}
  
TEST(JSONPackTest, SinglePropJSONPack) {
  EXPECT_EQ(std::string {"{\"foo\":\"bar\"}"},
	    fiasco::PackJSON(fiasco::JSONData {
	      {std::string("foo"),std::string("bar")}
	    }));
}

TEST(JSONPackTest, MultiPropJSONPack) {
  fiasco::JSONData json {
	      {std::string("foo"),std::string("bar")},
	      {std::string("foo2"),std::string("bar2")}    
  };
  EXPECT_EQ(std::string {"{\"foo\":\"bar\",\"foo2\":\"bar2\"}"},
	    fiasco::PackJSON(json));
}

TEST(JSONArrayTest, EmptyArray) {
  EXPECT_EQ(std::string {"[]"},
	    fiasco::PackJSONArray(std::vector<std::string> {}));
}

TEST(JSONArrayTest, Singleton) {
  EXPECT_EQ(std::string {"[foo]"},
	    fiasco::PackJSONArray(std::vector<std::string> {"foo"}));
}

TEST(JSONArrayTest, PlainArray) {
  std::vector<std::string> arr { "foo", "bar"};
  EXPECT_EQ(std::string {"[foo,bar]"},
	    fiasco::PackJSONArray(arr));
}

TEST(SplitTest, EmptyString) {
  EXPECT_EQ(0, fiasco::SplitIntoViews("").size());
}

TEST(SplitTest, SingleLineNoTerminator) {
  auto split = fiasco::SplitIntoViews("Hello");
  EXPECT_EQ(1, split.size());
}

TEST(SplitTest, SingleLineTerminator) {
  auto split = fiasco::SplitIntoViews("Hello, World\n");
  EXPECT_EQ(1, split.size());
  EXPECT_EQ(12, split[0].length);
}

TEST(SplitTest, MultilineNoTerminator) {
  std::string test_str {"Hello,World"};
  auto split = fiasco::SplitIntoViews(test_str,',');
  EXPECT_EQ(2,split.size());
  EXPECT_EQ(5,split[0].length);
  EXPECT_EQ(5,split[1].length);
  EXPECT_EQ(6,split[1].offset);
}

TEST(SplitTest, MultilineWithTerminator) {
  std::string test_str {"Hello,World,"};
  auto split = fiasco::SplitIntoViews(test_str,',');
  EXPECT_EQ(2,split.size());
  EXPECT_EQ(5,split[0].length);
  EXPECT_EQ(5,split[1].length);
  EXPECT_EQ(6,split[1].offset);
}

TEST(CSVTypesTest, EmptyRow) {
  EXPECT_EQ(0,fiasco::DetectTypes("").size());
}

TEST(CSVTypesTest, Int) {
  auto row = fiasco::DetectTypes("5");
  EXPECT_EQ(1,row.size());
  EXPECT_EQ(fiasco::Types::Int,row[0]);
}

TEST(CSVTypesTest, Float) {
  auto row = fiasco::DetectTypes("5.0");
  EXPECT_EQ(1,row.size());
  EXPECT_EQ(fiasco::Types::Float,row[0]);
}

TEST(CSVTypesTest, MinusInt) {
  auto row = fiasco::DetectTypes("-5");
  EXPECT_EQ(1,row.size());
  EXPECT_EQ(fiasco::Types::Int,row[0]);
}

TEST(CSVTypesTest, MinusFloat) {
  auto row = fiasco::DetectTypes("-5.0");
  EXPECT_EQ(1,row.size());
  EXPECT_EQ(fiasco::Types::Float,row[0]);
}

TEST(CSVTypesTest, DoubleMinusInt) {
  auto row = fiasco::DetectTypes("--5");
  EXPECT_EQ(1,row.size());
  EXPECT_EQ(fiasco::Types::String,row[0]);
}

TEST(CSVTypesTest, ProperCSVRow) {
  using Type = fiasco::Types;
  
  std::string row {"201501BS70001,525130,180050,-0.198465,51.505538,1,3,1,1,12/01/2015,2,18:45,12,E09000020,5,0,6,30,3,4,6,0,0,0,4,1,1,0,0,1,1,E01002825"};
  std::vector<Type> row_types {
    Type::String, Type::Int, Type::Int, Type::Float, Type::Float, Type::Int, Type::Int, Type::Int,
    Type::Int, Type::String, Type::Int, Type::String, Type::Int, Type::String, Type::Int, Type::Int,
    Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::Int,
    Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::Int, Type::String
  };
  auto types = fiasco::DetectTypes(row);
  EXPECT_EQ(row_types.size(),types.size());
  for (size_t ind = 0;ind < row_types.size() ;++ind) {
    EXPECT_EQ(row_types[ind],types[ind]);
  }
}

// Unfortunately testing more than 2 is unreliable because we use randomized algorithm
// So an adversary can always create a condition in which test succeds or fails independent of correctness of actual algorithm
TEST(CSVCorrect, TwoLineCSV) {
  std::string test_ok {"100,100\n 50,50"};
  std::string test_notok {"100,100\n 50,50,50"};

  EXPECT_TRUE(fiasco::SampleForCSV(test_ok));
  EXPECT_FALSE(fiasco::SampleForCSV(test_notok));
}


// At last, JSON parsing test

TEST(JSONParse, SingleEntryJSON) {
  std::string basic_valid_json {
    "{\"param1\":\"val1\"}"
  };
  std::string basic_invalid_json {
    "{\"param1\":\"val1\",}"
  };
  auto maybe_json = fiasco::ParseStrictJSON(basic_valid_json);
  auto certainly_not_json = fiasco::ParseStrictJSON(basic_invalid_json);
  EXPECT_FALSE(certainly_not_json);
  EXPECT_TRUE(maybe_json);
  if (maybe_json) {
    EXPECT_EQ((*maybe_json)["param1"],"val1");
  }
}

TEST(JSONParse, TwoEntryJSON) {
  std::string basic_valid_json {
    "{\"param1\":\"val1\",\"param2\":\"val2\"}"
  };
  std::string basic_invalid_json {
    "{\"param1\":\"val1\"\"param2\":\"val2\"}"
  };
  auto maybe_json = fiasco::ParseStrictJSON(basic_valid_json);
  auto certainly_not_json = fiasco::ParseStrictJSON(basic_invalid_json);
  EXPECT_FALSE(certainly_not_json);
  EXPECT_TRUE(maybe_json);
  if (maybe_json) {
    EXPECT_EQ((*maybe_json)["param2"],"val2");
  }
}

TEST(JSONParse, ScreenedJSON) {
  std::string basic_screened_json {
    "{\"param1\":\"\\\"screened_value\\\"\"}"
  };
  auto maybe_json = fiasco::ParseStrictJSON(basic_screened_json);
  EXPECT_TRUE(maybe_json);
  if (maybe_json) {
    EXPECT_EQ((*maybe_json)["param1"],"\"screened_value\"");
  }
}
