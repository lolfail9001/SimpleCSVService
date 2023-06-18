#ifndef __SQLITECOMMANDS__
#define __SQLITECOMMANDS__

namespace fiasco {
  //Prepare CSV table
  static const std::string kTablesTable("CREATE TABLE main.Files ( name varchar(30) NOT NULL, internal_name varchar(64) NOT NULL, creator_id INTEGER NOT NULL);");
  //Prepare permissions table
  static const std::string kUsersTable("CREATE TABLE main.Users ( name varchar(30) NOT NULL PRIMARY KEY, role INTEGER NOT NULL);");
  //Setup an admin user
  static const std::string kAdminSetup("INSERT INTO main.Users ( name, role ) VALUES ( \"admin\", 1);");
  //User management
  static const std::string kCheckUser("SELECT role,rowid FROM main.Users WHERE name = ?;");
  //Uploading CSVs
  static const std::string kTableTemplate("CREATE TABLE main.{} ({});");
  static const std::string kUserReg("INSERT INTO main.Users (name,role) VALUES (?,2) RETURNING rowid");
  static const std::string kTableReg("INSERT INTO main.Files (name,internal_name,creator_id) VALUES (?,?,?)");
  static const std::string kTableContent("INSERT INTO main.{} VALUES {};");
  //Querying list of CSVs + their columns + list of them user can delete
  static const std::string kTableQuery("SELECT name FROM main.Files;");
  static const std::string kColTemplate("PRAGMA main.table_info({});");
  static const std::string kPermTables("SELECT Files.name from main.Files "
				       "INNER JOIN main.Users ON "
				       "Files.creator_id = Users.rowid "
				       "WHERE Users.name = {} AND "
				       "Users.role != 3;");
  static const std::string kUpdate("UPDATE main.{} SET {} WHERE rowid={}");
  
  //Querying the CSV as contained in DB is basically all data and no template  
}

  
#endif
