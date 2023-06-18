#include "csvloadapp.hpp"
#include <iostream>

void AttemptBind(const char *param,const char* val,std::string& ipaddr, int& port, std::string& db_name) {
  if (std::string(param) == std::string("-p")) {
    auto buf = std::string(val);
    if (std::count(buf.begin(),buf.end(),',') > 0 ||
	fiasco::DetectTypes(buf)[0] != fiasco::Types::Int) {
      throw std::exception();
    }
      else {
      port = std::atoi(val);
    }
  }
  if (std::string(param) == std::string("-db")) {
    db_name = val;
  }  
}

int main(int argc,const char **args) {
  std::ios_base::sync_with_stdio(false);
  int port = 8000;
  std::string ipaddr = "127.0.0.1";
  std::string db_name = "csvs.db";

  try {
    if (argc >= 3) {
      AttemptBind(args[1],args[2],ipaddr,port,db_name);
    }
    if (argc >= 5) {
      AttemptBind(args[3],args[4],ipaddr,port,db_name);
    }
  }
  catch(std::exception& e) {
    std::cerr << "Invalid port binding. Running on default port 8000\n";
  }
  
  fiasco::CSVApp app(db_name,false);
  app.Run(ipaddr,port);

  return 0;
}
