#ifndef __CSVLOADAPP__
#define __CSVLOADAPP__

extern "C"
{
#include <sqlite3.h>
}

#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include "cpp-httplib/httplib.h"


// Rule is simple
// We declare types we need, every /pure/ function and the classes for stateful computation
namespace fiasco {
  using JSONData = std::map<std::string,std::string>;

  enum Role {
    Readonly = 3,
    Normal = 2,
    Admin = 1,
    None = 0
  };
  enum Types {
    String = 2,
    Int = 1,
    Float = 0
  };

  struct Substring {
    uint32_t offset;
    uint32_t length;
  };

  //One is really tempted to define a JSON object to deal with
  //Conceptually it is one of
  //map: key -> string | JSON object | array
  //array string | JSON object
  //string
  //Then one defines tail-recursive serialization

  std::string PackJSON(const JSONData& obj);
  std::string PackJSONArray(const std::vector<std::string>& arr);
  //returns either a JSONData object or none
  std::optional<JSONData> ParseStrictJSON(const std::string& serialized);
  
//  std::vector<size_t> OffsetsForSplit(const std::string& multiline, char separator = '\n');
  std::vector<Substring> SplitIntoViews(const std::string& str, char separator = '\n');
  bool SampleForCSV(const std::string& csv_content, char separator = ',',size_t samples = 10);
  std::vector<Types> DetectTypes(const std::string& csv_row, char separator = ',');

  
  class CSVApp {
  private:
    sqlite3 *db_handle_;
    httplib::Server svr_;



    void TryExecSimpleQuery(const std::string& query);

    void DbSetup();
    Role DbCheckUser(const std::string& username);
    int DbRegUser(const std::string& username);
    void DbUpload(const std::string& name,
		  const std::string& content,
		  char separator = ',',
		  size_t batch_size = 100);
    std::vector<std::string> DbQueryTableList();
    std::vector<std::string> DbQueryColList(const std::string& table_name);
    JSONData DbQueryList();
    uint32_t DbQueryTableSize(const std::string& table_name);
    JSONData DbQueryTable(const std::string& table_name,
			  const std::vector<std::string>& cols,
			  const std::unordered_map<std::string,bool> sorts,
			  const std::pair<uint32_t,uint32_t> row_range);
    void DbDeleteTable(const std::string& table_name);
    void DbUpdateRow(const std::string& name,
		     const JSONData& row,
		     uint32_t at_row);
    void DbAppendRow(const std::string& name,
		     const JSONData& row);

    void HandleUpload(const httplib::Request& req,
		      httplib::Response& res);
    void HandleDeletion(const httplib::Request& req,
			httplib::Response& res);
    void HandleListing(const httplib::Request& req,
		       httplib::Response& res);
    void HandleTableQuery(const httplib::Request& req,
			  httplib::Response& res);
    void HandleTableUpdate(const httplib::Request& req,
			   httplib::Response& res);
    
  public:
    //Setup the DB if need be and setup request handlers
    CSVApp(const std::string& db_file, bool in_memory = false);
    virtual ~CSVApp();
    //Passthrough to svr.listen()
    void Run(std::string addr, int port);
  };
}
  
#endif
