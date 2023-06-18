#include "csvloadapp.hpp"
#include <cctype>
#include <filesystem>
#include <random>
#include <charconv>
#include <string_view>
#include <format>

#include "sqlitecommands.hpp"


namespace fiasco {
  std::string PackJSON(const JSONData& obj) {
    std::stringstream ss;
    ss << "{";
    for (const auto& elem : obj) {
      ss << "\"" << elem.first << "\":\"" << elem.second << "\",";
    }
    if (!obj.empty()) {
      ss.seekp(-1,std::ios_base::end);
    }
    ss << "}";

    return ss.str();
  }
  std::string PackJSONArray(const std::vector<std::string>& arr) {
    std::stringstream ss;
    ss << "[";
    for (const auto& elem : arr) {
      ss << elem << ",";
    }
    if (!arr.empty()) {
      ss.seekp(-1,std::ios_base::end);
    }
    ss << "]";

    return ss.str();
  }

  // Parse the simple JSONs of format {"param1":"val1","param2":"val2"}
  // It is assumed that with strings quotes are backslashed
  // One wishes C++ had proper pattern matching in these moments
  // I'll additionally observe that at high end one just automates generating this boiler plate code
  std::optional<JSONData> ParseStrictJSON(const std::string& serialized) {
    enum ParserState {
      JSONStart,
      ParamStringEntry,
      ParamStringExit,
      ValStringEntry,
      ValStringExit,
      JSONNextLine
    };
    JSONData current_data;
    ParserState current_state = JSONStart;
    std::string buf_key,buf_val;
    for (size_t ind = 0; ind < serialized.length(); ++ind) {
      char cur_char = serialized[ind];
      switch (current_state) {
      case JSONStart:
	if (std::isspace(cur_char) || cur_char == '{') {}
	else if (cur_char == '"') {
	  current_state = ParamStringEntry;
	  buf_key.clear();
	}
	else {
	  return std::optional<JSONData>();
	}
	break;
      case ParamStringEntry:
	if (cur_char == '\\' && (ind + 1) < serialized.length()) {
	  buf_key += serialized[ind + 1];
	  ++ind;
	}
	else if (cur_char == '"') {
	  current_state = ParamStringExit;
	}
	else {
	  buf_key += cur_char;
	}
	break;
      case ParamStringExit:
	if (std::isspace(cur_char)) {}
	else if (cur_char == ':') {}
	else if (cur_char == '"') {
	  current_state = ValStringEntry;
	  buf_val.clear();
	}
	else {
	  return std::optional<JSONData>();
	}
	break;
      case ValStringEntry:
	if (cur_char == '\\' && (ind + 1) < serialized.length()) {
	  buf_val += serialized[ind + 1];
	  ++ind;
	}
	else if (cur_char == '"') {
	  current_state = ValStringExit;
	}
	else {
	  buf_val += cur_char;
	}
	break;
      case ValStringExit:
	if (std::isspace(cur_char)) {}
	else if (cur_char == ',') {
	  current_state = JSONNextLine;
	  current_data[buf_key] = buf_val;
	}
	else if(cur_char == '}') {
	  current_data[buf_key] = buf_val;
	  return std::optional<JSONData>(current_data);
	}
	else {
	  return std::optional<JSONData>();
	}
	break;
      case JSONNextLine:
	if (std::isspace(cur_char)) {}
	else if (cur_char == '"') {
	  current_state = ParamStringEntry;
	  buf_key.clear();
	}
	else {
	  return std::optional<JSONData>();
	}
	break;
      }

    }

    return std::optional<JSONData>(current_data);
    }
  
  std::vector<Substring> SplitIntoViews(const std::string& str,char separator) {
    if (str.empty()) {
      return {};
    }
    auto it = str.find(separator,0);
    if (it == std::string::npos) {
      return {{0,static_cast<uint32_t>(str.length())}};
    }
    std::vector<Substring> result;
    result.push_back({0,static_cast<uint32_t>(it)});
    while (it < str.length()-1) {
      Substring tmp;
      tmp.offset = it + 1;
      it = str.find(separator,it + 1);
      if (it == std::string::npos) {
	it = str.length();
      }
      tmp.length = it - tmp.offset;
      result.push_back(tmp);
    }
    return result;
  }

  // Serves primarily testing purposes, same as in-memory interface
  // In production you just run the db setup script before any actual usage
  void CSVApp::DbSetup() {
    TryExecSimpleQuery(kUsersTable);
    TryExecSimpleQuery(kTablesTable);
    TryExecSimpleQuery(kAdminSetup);
  }


  CSVApp::CSVApp(const std::string& db_file, bool in_memory) : svr_() {
    int flag;
    if (in_memory) {
      flag = SQLITE_OPEN_READWRITE | SQLITE_OPEN_MEMORY;
    }
    else if (std::filesystem::exists(db_file)) {
      flag = SQLITE_OPEN_READWRITE;
    }
    else {
      flag = SQLITE_OPEN_CREATE | SQLITE_OPEN_READWRITE;
    }
    if (sqlite3_open_v2(db_file.c_str(),&db_handle_,flag,nullptr) != SQLITE_OK) {
      std::cerr << "In sqlite3_open\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }

    if ((flag & SQLITE_OPEN_CREATE) != 0) {
      DbSetup();
    }

    //Connect handlers
    svr_.Get("/tables",[this](const httplib::Request& req,httplib::Response& res) {
      res.body = PackJSON(this->DbQueryList());
      res.status = 200;
    });
    svr_.Get("/view",[this](const httplib::Request& req,httplib::Response& res) {
      this->HandleTableQuery(req,res);
    });
    svr_.Post("/upload",[this](const httplib::Request& req,httplib::Response& res) {
      this->HandleUpload(req,res);
    });
    svr_.Get("/delete",[this](const httplib::Request& req,httplib::Response& res) {
      //Authorize
      //Verify
      //Respond
    });
    svr_.Post("/update",[this](const httplib::Request& req,httplib::Response& res) {
      this->HandleTableUpdate(req,res);
    });
  }

  CSVApp::~CSVApp() {
    sqlite3_close(db_handle_);
  }


  Role CSVApp::DbCheckUser(const std::string& username) {
    sqlite3_stmt* query;
    auto role = Role::None;
    if(sqlite3_prepare(db_handle_,
		       kCheckUser.c_str(),
		       kCheckUser.length() + 1,
		       &query,
		       nullptr) != SQLITE_OK) {
      std::cerr << "In DbCheckUser::sqlite3_prepare\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    if(sqlite3_bind_text(query,
			 1,
			 username.c_str(),
			 username.length() + 1,
			 SQLITE_STATIC) != SQLITE_OK) {
      std::cerr << "In DbCheckerUser::sqlite3_bind username\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    switch(sqlite3_step(query)) {
    case SQLITE_ROW:
      role = static_cast<Role>(sqlite3_column_int(query,1));
      break;
    case SQLITE_DONE:
      break;
    default:
      std::cerr << "In DbCheckUser::sqlite3_step";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
      break;
    }
    if (sqlite3_finalize(query) != SQLITE_OK) {
      std::cerr << "In DbCheckUser::sqlite3_finalize";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    return role;
  }
  
  int CSVApp::DbRegUser(const std::string& username) {
    sqlite3_stmt *query;
    int id = -1;
    if(sqlite3_prepare_v2(db_handle_,
			  kUserReg.c_str(),
			  kUserReg.length()+1,
			  &query,
			  nullptr) != SQLITE_OK) {
      std::cerr << sqlite3_errmsg(db_handle_);
    }
    if(sqlite3_bind_text(query,
			 1,
			 username.c_str(),
			 username.length()+1,
			 SQLITE_STATIC) != SQLITE_OK) {
      std::cerr << sqlite3_errmsg(db_handle_);
    }
    if(sqlite3_step(query) != SQLITE_ROW) {
      std::cerr << sqlite3_errmsg(db_handle_);
    }
    else {
      id = sqlite3_column_int(query,0);
    }
    if(sqlite3_finalize(query) != SQLITE_OK) {
      std::cerr << sqlite3_errmsg(db_handle_);
    }
    return id;
  }

  //Randomized test for being a proper CSV, namely that separator count on each line matches up
  //TODO: Check proper indexing
  bool SampleForCSV(const std::string& csv_content, char separator,size_t samples) {
    auto lines = SplitIntoViews(csv_content);
    //Check directly for small csvs
    if (lines.size() <= 1) {
      return true;
    }
    auto fst_line = csv_content.substr(lines[0].offset,lines[0].length);
    if (lines.size() == 2) {
      auto lst_line = csv_content.substr(lines[1].offset,lines[1].length);
      return std::count(fst_line.begin(),fst_line.end(),separator) ==
	     std::count(lst_line.begin(),lst_line.end(),separator);
    }

    std::random_device rd;
    std::mt19937 gen32(rd());
    std::uniform_int_distribution<> dist(1,lines.size());

    
    size_t base_cols = std::count(fst_line.begin(),fst_line.end(),separator);
    for (size_t ind = 0;ind < samples; ++ind) {
      auto line_num = dist(gen32);
      auto test_line = csv_content.substr(lines[line_num].offset,lines[line_num].length);
      size_t test_cols =  std::count(test_line.begin(),test_line.end(),separator);
      if (base_cols != test_cols) {
	return false;
      }
    }
    
    return true;
  }
  
  //Returns SQL types as enum members
  //I question whether i truly need to go so far
  //Ahaha, of course I do, i am not going to use lexigraphic sort on floats
  std::vector<Types> DetectTypes(const std::string& csrow, char separator) {
    auto cols = SplitIntoViews(csrow,separator);
    std::vector<Types> result;
    for (const auto& col : cols) {
      auto str = csrow.substr(col.offset,col.length);
      auto dot_cnt = 0;
      for (size_t ind = 0;ind < str.length() ; ++ind) {
	if (str[ind] == '-' && ind == 0) {
	  continue;
	} 
	if (str[ind] == '.') {
	  ++dot_cnt;
	}
	else if (std::isdigit(str[ind])) {
	  continue;
	}
	else {
	  dot_cnt = 10;
	  break;
	}
      }
      if (dot_cnt > 1) {
	result.push_back(Types::String);
      }
      else if (dot_cnt == 1) {
	result.push_back(Types::Float);
      }
      else {
	result.push_back(Types::Int);
      }
    }
    return result;
  }

  void CSVApp::TryExecSimpleQuery(const std::string& query) {
    std::cerr << "Query:" << query << "\n";
    sqlite3_stmt* stmt_handle;
    if(sqlite3_prepare_v2(db_handle_,
			  query.c_str(),
			  query.length() + 1,
			  &stmt_handle,
			  nullptr) != SQLITE_OK) {
      std::cerr << "In TryExecSimpleQuery::prepare\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    if(sqlite3_step(stmt_handle) != SQLITE_DONE) {
      std::cerr << "In TryExecSimpleQuery::step\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
      }
    if(sqlite3_finalize(stmt_handle) != SQLITE_OK) {
      std::cerr << "In TryExecSimpleQuery:finalize";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
  }

  
  //TODO: Implement batch inserting
  void CSVApp::DbUpload(const std::string& name,
			const std::string& content,
			char separator,
			size_t batch_size) {
    //First of all, we will have a whole lot of substring operations here.
    //Most low-level improvement for this would be to manipulate pointer for content.data()
    //With careful track of length
    //Unfortunately, no way in hell can I do that in feasible timeframe
    
    //We assume it is a proper CSV, beauty of private implementation
    //Furthermore we assume user permissions are intact
    size_t fstnewline = content.find('\n',0);
    bool create_only = false;
    auto header = content.substr(0,fstnewline);
    std::vector<Types> types = DetectTypes(header);
    bool has_signature =
      (std::find_if(types.begin(),types.end(),[](auto t){
	return t != Types::String;
      }) == types.end());
    if (fstnewline == content.size() && has_signature) {
      create_only = true;
    }
    if (has_signature) {
      size_t nextline = content.find('\n',fstnewline+1);
      auto sndline = content.substr(fstnewline+1,
				    nextline - fstnewline);
      types = DetectTypes(sndline);
    }

    std::stringstream col_descr;
    // One would likely assert that col_offsets.size() == types.size();
    // But this is implied if col_offsets is computed properly by correct CSV
    if (!has_signature) {
      for (size_t ind = 0 ; ind < types.size() ; ++ind) {
	bool last = (ind == (types.size() -1));
	col_descr << "\"" << ind + 1 << "\" ";
	switch (types[ind]) {
	case Types::String:
	  col_descr << "varchar(255)" << (last?" ":", ");
	  break;
	case Types::Float:
	  col_descr << "float" << (last?" ":", ");
	  break;
	case Types::Int:
	  col_descr << "int" << (last?" ":", ");
	  break;
	}	
      }
    }
    else if(create_only) {
      auto col_view = SplitIntoViews(header,separator);
      for (size_t ind = 0; ind < col_view.size() ; ++ind) {
	bool last = (ind == col_view.size() - 1);
	col_descr << header.substr(col_view[ind].offset,
				   col_view[ind].length)
	<< " varchar(255) " << (last?", ":" ");
      }
    }
    else {
      auto col_view = SplitIntoViews(header,separator);
      for (size_t ind = 0; ind < col_view.size() ; ++ind) {
	bool last = (ind == col_view.size() - 1);
	col_descr << "\"" << header.substr(col_view[ind].offset,
				   col_view[ind].length) << "\" ";

	switch (types[ind]) {
	case Types::String:
	  col_descr << "varchar(255) ";
	    break;
	case Types::Float:
	  col_descr << "float ";
	  break;
	case Types::Int:
	  col_descr << "int ";
	  break;
	}
	col_descr << (last?", ":" ");
      }
    }

    auto col_str = col_descr.str();

    std::stringstream table_create_ss;
    table_create_ss << "CREATE TABLE main.\"" << name << "\" (" << col_str <<");";

    //Execute this query
    TryExecSimpleQuery(table_create_ss.str());
    //Register the table as is
    std::stringstream register_ss;
    register_ss << "INSERT INTO main.Files VALUES(\"" << name  <<"\",\"testtesttest\",1);";
    TryExecSimpleQuery(register_ss.str());

    //The virgin version: row-by-row insert calls for each csv row
    //The chad version: batch insert with mass bind for safety and performance
    //I'm still a virgin though
    if (!create_only) {
      auto lines = SplitIntoViews(content);
      for (size_t ind = has_signature?1:0; ind < lines.size() ; ++ind) {
	//For whatever reason Apple's stdc++ header does not have std::format
	std::stringstream table_insert_ss;
        table_insert_ss
	<< "INSERT INTO main.\"" << name << "\" VALUES("
        << content.substr(lines[ind].offset,lines[ind].length)
	<< ");";

	TryExecSimpleQuery(table_insert_ss.str());
      }
    }
    
  }

  //This will fail miserably with non-ASCII table names
  //Solution? Refuse to upload CSVs under non-ASCII names :-)

  std::vector<std::string> CSVApp::DbQueryTableList() {
    sqlite3_stmt *table_list_query;
    std::cerr << "Query:" << kTableQuery << "\n";
    std::cerr << "Query length:" << kTableQuery.length() << "\n";
    if (sqlite3_prepare_v2(db_handle_,
			   kTableQuery.c_str(),
			   kTableQuery.length() + 1,
			   &table_list_query,
			   nullptr) != SQLITE_OK) {
      std::cerr << "In DbQueryTableList::prepare\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    std::vector<std::string> table_names;
    while (sqlite3_step(table_list_query) == SQLITE_ROW) {
      auto table_name = sqlite3_column_text(table_list_query,0);
      table_names.emplace_back(reinterpret_cast<const char*>(table_name));
    }
    
    //Finalization to reuse table_list_query later
    //Who needs many pointer when one pointer do the thing?
    if (sqlite3_finalize(table_list_query) != SQLITE_OK) {
      std::cerr << "In DbQueryTableList::finalize\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }

    return table_names;
  }

  std::vector<std::string> CSVApp::DbQueryColList(const std::string& table_name) {
    sqlite3_stmt *table_col_query;
    std::string col_query ("SELECT name FROM pragma_table_info('");
    col_query += table_name;
    col_query += "');";

    std::cerr << "Query:" << col_query << "\n";
    if (sqlite3_prepare_v2(db_handle_,
			   col_query.c_str(),
			   col_query.length() + 1,
			   &table_col_query,
			   nullptr) != SQLITE_OK) {
      std::cerr << "In DbQueryColList::prepare\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    std::vector<std::string> cols;
    while(sqlite3_step(table_col_query) == SQLITE_ROW) {
      auto col_name = sqlite3_column_text(table_col_query,0);
      cols.emplace_back(reinterpret_cast<const char*>(col_name));
    }
    
    if (sqlite3_finalize(table_col_query) != SQLITE_OK) {
      std::cerr << "In DbQueryColList::finalize\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    return cols;
  }
  
  JSONData CSVApp::DbQueryList() {
    JSONData list;
    //And once again i cry over lack of std::format
    auto table_names = DbQueryTableList();
    std::vector<std::string> tables_json;
    for (const auto& table : table_names) {
      JSONData table_desc;
      auto cols = DbQueryColList(table);
      table_desc[table] = PackJSONArray(cols);
      tables_json.push_back(PackJSON(table_desc));
    }
    list["tables"] = PackJSONArray(tables_json);
    return list;
  }

  //Once again we offload the authorization duty to handler
  JSONData CSVApp::DbQueryTable(const std::string& table_name,
				const std::vector<std::string>& cols,
				const std::unordered_map<std::string,bool> sorts,
				const std::pair<uint32_t,uint32_t> row_range) {
    JSONData table;
    
    //Build the query string
    std::stringstream query_ss;
    query_ss << "SELECT ";

    std::vector<std::string> real_cols;
    if (cols.empty()) {
      real_cols = DbQueryColList(table_name);
    }
    const std::vector<std::string>& used_cols = (cols.empty())?real_cols:cols;
    for (size_t ind = 0;ind <used_cols.size() ;++ind) {
      bool last = (ind + 1 == used_cols.size());
      query_ss << "\"" << used_cols[ind] << (last?"\" ":"\", ");
    }
    query_ss << "FROM main.\"" << table_name << "\" ";
    if(!sorts.empty()) {
      query_ss <<" ORDER BY ";
      for(const auto sort : sorts) {
	query_ss << "\"" << sort.first << "\" " << (sort.second?"ASC,":"DESC,");
      }
      query_ss.seekp(-1,std::ios::end);
    }
    query_ss << " WHERE rowid >= " << row_range.first <<
    " AND rowid <= " << row_range.second << ";";
    auto query_str = query_ss.str();
    //Execute query and pack it into JSON
    std::cerr << "View query:" << query_str << "\n";

    sqlite3_stmt *query;
    if (sqlite3_prepare_v2(db_handle_,
			   query_str.c_str(),
			   query_str.length() + 1,
			   &query,
			   nullptr) != SQLITE_OK) {
      std::cerr << "In DbQueryTable::prepare\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }

    std::vector<std::string> rows_json;
    while(sqlite3_step(query) == SQLITE_ROW) {
      JSONData row;      
      for (size_t ind = 0; ind < used_cols.size() ;++ind) {
	auto type = sqlite3_column_type(query,ind);
	switch (type) {
	case SQLITE_INTEGER:
	  row[used_cols[ind]] = std::to_string(sqlite3_column_int(query,ind));
	  break;
	case SQLITE_FLOAT:
	  row[used_cols[ind]] = std::to_string(sqlite3_column_double(query,ind));
	  break;
	case SQLITE_TEXT:
	  row[used_cols[ind]] = std::string {
	    reinterpret_cast<const char*>(sqlite3_column_text(query,ind))
	  };
	  break;
	case SQLITE_NULL:
	  row[used_cols[ind]] = "";
	default:
	  break;
	}
      }
      rows_json.push_back(PackJSON(row));
    }

    if (sqlite3_finalize(query) != SQLITE_OK) {
      std::cerr << "In DbQueryTable::finalize\n";
      std::cerr << sqlite3_errmsg(db_handle_);
      std::cerr << "\n";
    }
    table["contents"] = PackJSONArray(rows_json);
    return table;
  }

  uint32_t CSVApp::DbQueryTableSize(const std::string& table_name) {
    std::string query_str = "SELECT count(*) FROM main.\"" + table_name + "\";";
    uint32_t result = 0;
    sqlite3_stmt *query;

    if (sqlite3_prepare_v2(db_handle_,
			   query_str.c_str(),
			   query_str.length() + 1,
			   &query,
			   nullptr) != SQLITE_OK) {
      std::cerr << "Query:" << query_str << "\n";
      std::cerr << sqlite3_errmsg(db_handle_) << "\n";
    }
    if (sqlite3_step(query) == SQLITE_ROW) {
      result = static_cast<uint32_t>(sqlite3_column_int(query,0));
    }
    else {
      std::cerr << sqlite3_errmsg(db_handle_) << "\n";
    }

    if (sqlite3_finalize(query) != SQLITE_OK) {
      std::cerr << sqlite3_errmsg(db_handle_) << "\n";
    }
    
    return result;
  }

  //Authorization assumed
  void CSVApp::DbDeleteTable(const std::string& table_name) {
    std::string drop_query_str = "DROP TABLE IF EXISTS main.\"";
    drop_query_str+=table_name;
    drop_query_str+="\";";
    std::string cleanup_query_str = "DELETE FROM main.Files WHERE name=\"";
    cleanup_query_str+=table_name;
    cleanup_query_str+="\";";

    TryExecSimpleQuery(drop_query_str);
    TryExecSimpleQuery(cleanup_query_str);
  }

  void CSVApp::HandleUpload(const httplib::Request& req,httplib::Response& res) {
    std::vector<std::string> issue_list;

    auto csv_name = req.files.find("csv_name")->second.content;
    if (csv_name.empty()) {
      issue_list.emplace_back("Empty name field, using filename as fallback.");
      csv_name = req.files.find("csv_file")->second.filename;
    }
    // Toss away authorization for time being

    auto& csv = req.files.find("csv_file")->second.content;
    if (!SampleForCSV(csv)) {
      issue_list.emplace_back("Invalid CSV. Aborted");
      res.status = 400;
      res.body = PackJSONArray(issue_list);
    }

    DbUpload(csv_name,csv);
    res.status = 200;
    res.body = PackJSONArray(issue_list);
  }

  //Leave validation to handlers
  void CSVApp::DbAppendRow(const std::string& name,
			   const JSONData& row) {
    std::string col_names {"("},col_values {"VALUES ("},query_str {"INSERT INTO main.\""};
    for (auto it = row.begin(); it != row.end(); ++it) {
      bool last = (std::distance(it,row.end()) == 1);
      col_names += it->first;
      col_values += it->second;
      if (last) {
	col_names += ")";
	col_values += ");";
      }
      else {
	col_names += ",";
	col_values += ",";
      }
    }

    query_str += name;
    query_str += "\" ";
    query_str += col_names;
    query_str += col_values;

    TryExecSimpleQuery(query_str);    
  }

  void CSVApp::DbUpdateRow(const std::string& name,
			   const JSONData& row,
			   uint32_t at_row) {
    std::string col_names {"("},col_values {"VALUES ("},query_str {"UPDATE main.\""};
    for (auto it = row.begin(); it != row.end(); ++it) {
      bool last = (std::distance(it,row.end()) == 1);
      col_names += it->first;
      col_values += it->second;
      if (last) {
	col_names += ")";
	col_values += ")";
      }
      else {
	col_names += ",";
	col_values += ",";
      }
    }

    query_str += name;
    query_str += "\" SET ";
    query_str += col_names;
    query_str += " = ";
    query_str += col_values;
    query_str += " WHERE rowid = ";
    query_str += std::to_string(at_row);
    query_str += ";";

    TryExecSimpleQuery(query_str);    

  }

  void CSVApp::HandleListing(const httplib::Request& req,httplib::Response& res) {
    res.status = 200;
    res.body = PackJSON(DbQueryList());
  }

  void CSVApp::HandleTableQuery(const httplib::Request& req,httplib::Response& res) {
    //Uses a whole lot of params
    //First: column numbers to display
    auto col_its = req.params.equal_range("col");
    //Second: column numbers that must be ordered by ascension
    auto asc_its = req.params.equal_range("asc");
    //Third: --//-- by descension
    auto desc_its = req.params.equal_range("desc");
    //Fourth: I kinda forgot about it, but the actual name of the table.
    auto name = req.params.find("name")->second;
    //Fifth: the row range
    auto from_row = req.params.find("from")->second;
    auto to_row = req.params.find("to")->second;

    //Now, a whole lotta checks
    std::vector<std::string> issue_list;

    //First step, table name verification
    //As bonus points, identification of columns we need to deal with
    auto table_list = DbQueryTableList();
    if (std::find(table_list.begin(),table_list.end(),name) == table_list.end()) {
      res.status = 400;
      res.body = "Can't find the named table";
      return;
    }
    //Second step, throw out invalid col,asc,desc numbers
    auto param_to_vec = [&issue_list](decltype(asc_its) its) {
      std::vector<uint32_t> inds;
      for (auto it = its.first;it != its.second; ++it) {
	if(DetectTypes(it->second)[0] == Types::Int) {
	  inds.push_back(std::atoi(it->second.c_str()));
	}
	else {
	  issue_list.emplace_back("Invalid indexing");
	}
      }
      return inds;
    };

    std::vector<uint32_t> col_inds = param_to_vec(col_its);
    std::vector<uint32_t> asc_inds = param_to_vec(asc_its);
    std::vector<uint32_t> desc_inds = param_to_vec(desc_its);

    auto col_list = DbQueryColList(name);
    std::vector<std::string> col_names;
    for(const auto& ind : col_inds) {
      if (ind >= col_list.size()) {
	issue_list.emplace_back("A column index out of bounds");
      }
      else {
	col_names.push_back(col_list[ind]);
      }
    }
    std::unordered_map<std::string,bool> sort_names;
    for (const auto& ind : asc_inds) {
      if (ind >= col_list.size()) {
	issue_list.emplace_back("A column index out of bounds");
      }
      else {
	sort_names[col_list[ind]] = true;
      }
    }
    for (const auto& ind : desc_inds) {
      if (ind >= col_list.size()) {
	issue_list.emplace_back("A column index out of bounds");
      }
      else {
	sort_names[col_list[ind]] = false;
      }
    }
    //At last, validate row limits
    uint32_t from,to;
    if (!from_row.empty() && DetectTypes(from_row)[0] == Types::Int) {
      from = std::atoi(from_row.c_str());
    }
    else {
      from = 0;
    }
    if (!to_row.empty() && DetectTypes(to_row)[0] == Types::Int) {
      from = std::atoi(to_row.c_str());
    }
    else {
      to = 100;
    }

    //With all that done, let us now build the proper query
    auto table = DbQueryTable(name,col_names,sort_names,{from,to});
    table["issues"] = PackJSONArray(issue_list);
    res.status = 200;
    res.body = PackJSON(table);
  }

  void CSVApp::HandleTableUpdate(const httplib::Request& req, httplib::Response& res) {
    auto name = req.params.find("name")->second;
    auto rowid_it = req.params.find("rowid");
    auto rowdata = req.files.find("row_desc");

    auto table_list = DbQueryTableList();
    if (std::find(table_list.begin(),table_list.end(),name) == table_list.end()) {
      res.status = 400;
      res.body = "Can't find the named table";
      return;
    }

    if (rowid_it == req.params.end() ||
	std::count(rowid_it->second.begin(),rowid_it->second.end(),',') > 0 ||
	DetectTypes(rowid_it->second)[0] != Types::Int) {
      res.status = 400;
      res.body = "Incorrect row placement";
      return;
    }

    if (rowdata == req.files.end()) {
      res.status = 400;
      res.body = "No row data available";
      return;
    }

    auto data = ParseStrictJSON(rowdata->second.content);
    if (!data) {
      res.status = 400;
      res.body = "Invalid JSON input";
      return;
    }

    //Validate column names.
    //Unfortunately validating types is a fucking pain in written framework as is now
    //General TODO: Make the DB operations at least Monadic, then backpropagating their errors will be way easier
    auto col_list = DbQueryColList(name);
    for (const auto& json_col : *data) {
      if (std::find(col_list.begin(),col_list.end(),json_col.first) == col_list.end()) {
	res.status = 400;
	res.body = "Invalid column name";
	return;
      }
    }

    auto table_sz = DbQueryTableSize(name);
    if (std::atoi(rowid_it->second.c_str()) > table_sz) {
      DbAppendRow(name,*data);
      res.status = 200;
      res.body = "Attempted row append";
    }
    else {
      DbUpdateRow(name,*data,std::atoi(rowid_it->second.c_str()));
      res.status = 200;
      res.body = "Attempted row update";
    }
    
    
  }

  void CSVApp::Run(std::string addr, int port) {
    svr_.listen(addr,port);
  }
  
}
