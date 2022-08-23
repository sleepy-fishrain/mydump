#include <iostream>
#include <mysql/mysql.h> //mysql提供的函数接口头文件 sudo apt-get install libmysqlclient-dev
#include <fstream>
#include <string.h>
// #define DUMP_VERSION '0.0.1'
using namespace std;
MYSQL *sql = nullptr;

const char* host = "139.9.190.145"; //主机名
const char* user = "root"; //用户名
const char* password = "123456"; //密码
const char* dbName = "test"; //数据库名称
int port = 3326; //端口号

bool connect_to_db(const char* host, const char* user, const char* password, const char* dbName, int port);
void write_header(fstream& sql_file);
int dump_all_tables_in_db(char *database);
int dump_all_databases();


int main() {

    if (!connect_to_db(host, user, password, dbName, port)){return 1;};//1.连接到数据库
    fstream output_file;
    output_file.open("output.txt", ios::out | ios::in | ios::trunc);
    write_header(output_file);
    mysql_query(sql, "FLUSH TABLES");//关闭所有的表
    mysql_query(sql, "FLUSH TABLES WITH READ LOCK"); //再加上一个全局读锁
    mysql_query(sql, "SET SESSION TRANSACTION ISOLATION " "LEVEL REPEATABLE READ") ;//设置会话隔离级别为REPEATABLE READ
    mysql_query(sql, "START TRANSACTION " "WITH CONSISTENT SNAPSHOT");//start_transaction 开启一致性事务
    // mysql_query(sql, "RESET MASTER");
    // mysql_refresh(sock, REFRESH_LOG);//刷新一下binlog，为了更好的区分开始dump的数据点和未来需要继续重放的增量位点。
    //const char *comment_prefix=(opt_master_data == MYSQL_OPT_MASTER_DATA_COMMENTED_SQL) ? "-- " : "";
    mysql_query(sql, "SHOW MASTER STATUS");// SHOW MASTER STATUS 获取位点。

    MYSQL_ROW row;
    MYSQL_RES *master;
    master = mysql_store_result(sql);
    row = mysql_fetch_row(master);
    mysql_free_result(master);
    mysql_query(sql, "UNLOCK TABLES");//拿到了位点之后，需要将锁释放掉，让 Server 恢复读写，也因此，需要执行 SQL UNLOCCK TABLES。而接下来，我们就是要分情况去 dump 数据了。
    dump_all_databases();















    // MYSQL_RES * res =mysql_store_result(sql);
    // cout<<mysql_num_rows(res)<<endl;
    // 关闭mysql











    mysql_close(sql);
    output_file.close();
    cout<<"OK"<<endl;
    return 0;
}




bool connect_to_db(const char* host, const char* user, const char* password, const char* dbName, int port){
    // 创建mysql对象
    sql = mysql_init(sql);
    if (!sql) {
        cout << "MySql连接组件初始化失败！" << endl;
        return false;
    }
    // 连接mysql
    sql = mysql_real_connect(sql, host, user, password, dbName, port, nullptr, 0);
    if (!sql) {
        cout << "MySql连接失败!" << endl;
        return false;
    }
    return true;
};
void write_header(fstream& sql_file)
{
      sql_file << "-- MySQL dump (TEST_VERSION) " <<"0.0.1"<< "\n--\n";
      sql_file << "-- Host: "<< host <<"    Database: "<<dbName<<"\n";
      sql_file << "-- ------------------------------------------------------\n";
    //这里本来要再输出mysql版本的，但是还没写。
}

// static int init_dumping(char *database,fstream& sql_file)
// {
//   if (!path && !opt_xml)
//   {
//     if (opt_databases || opt_alldbs)
//     {
// 	  sql_file<<"\n--\n-- Current Database: "<<database<<"\n--\n";
    
//       if (!opt_create_db)
//       {
//         char qbuf[256];
//         MYSQL_ROW row;
//         MYSQL_RES *dbinfo;
//         strcpy(qbuf,"SHOW CREATE DATABASE ");
//         strcat(qbuf,database);
//         mysql_query(sql, qbuf);
//         dbinfo = mysql_store_result(sql);
// 	      row = mysql_fetch_row(dbinfo);
// 	  if (row[1])
// 	  {
// 	    fprintf(md_result_file,"\n%s;\n",row[1]);
//           }
	
//       }
//       fprintf(md_result_file,"\nUSE %s;\n", qdatabase);
//       check_io(md_result_file);
//     }
//   }
//   if (extended_insert && init_dynamic_string(&extended_row, "", 1024, 1024))
//     exit(EX_EOM);
//   return 0;
// } /* init_dumping */

// int dump_all_tables_in_db(char *database)
// {
//   char *table;
//   uint numrows;
//   char table_buff[NAME_LEN*2+3];

//   char hash_key[2*NAME_LEN+2];  /* "db.tablename" */
//   char *afterdot;

//   afterdot= strmov(hash_key, database);
//   *afterdot++= '.';

//   if (init_dumping(database))
//     return 1;
//   if (lock_tables)
//   {
//     DYNAMIC_STRING query;
//     init_dynamic_string(&query, "LOCK TABLES ", 256, 1024);
//     for (numrows= 0 ; (table= getTableName(1)) ; numrows++)
//     {
//       dynstr_append(&query, quote_name(table, table_buff, 1));
//       dynstr_append(&query, " READ /*!32311 LOCAL */,");
//     }
//     if (numrows && mysql_real_query(sock, query.str, query.length-1))
//       DBerror(sock, "when using LOCK TABLES");
//             /* We shall continue here, if --force was given */
//     dynstr_free(&query);
//   }
//   if (flush_logs)
//   {
//     if (mysql_refresh(sock, REFRESH_LOG))
//       DBerror(sock, "when doing refresh");
//            /* We shall continue here, if --force was given */
//   }
//   while ((table= getTableName(0)))
//   {
//     char *end= strmov(afterdot, table);
//     if (include_table(hash_key, end - hash_key))
//     {
//       numrows = getTableStructure(table, database);
//       if (!dFlag && numrows > 0)
// 	dumpTable(numrows,table);
//       my_free(order_by, MYF(MY_ALLOW_ZERO_PTR));
//       order_by= 0;
//     }
//   }
//   if (opt_xml)
//   {
//     fputs("</database>\n", md_result_file);
//     check_io(md_result_file);
//   }
//   if (lock_tables)
//     mysql_query_with_error_report(sock, 0, "UNLOCK TABLES");
//   return 0;
// } /* dump_all_tables_in_db */
int dump_all_databases()
{
  MYSQL_ROW row;
  MYSQL_RES *tableres;
  int result=0;

  if (mysql_query(sql,"SHOW DATABASES"))
    return 1;
  tableres = mysql_store_result(sql);
  while ((row = mysql_fetch_row(tableres)))
  {

			cout << row[0]<<endl;
    // if (dump_all_tables_in_db(row[0]))
    //   result=1;
  }
  return result;
}
