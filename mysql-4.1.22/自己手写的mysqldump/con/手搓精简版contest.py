
import pymysql
import decimal
host='139.9.190.145'
user='root'
password='123456'
database='test'
port=3336
sql = pymysql.connect(host=host, user=user, password=password, database=database, port=port)
cursor = sql.cursor()
out_file = open("output.txt",'w+',encoding='utf8')
insert_query = ""


def query_commit(sql_l):
	cursor.execute(sql_l)
	sql.commit()
def query(sql_l):
	cursor.execute(sql_l)



def init_dump(db):#建库
	out_file.write(f"\n--\n-- Current Database: {db}\n--\n")
	sql.select_db(db)
	query(f"SHOW CREATE DATABASE {db}")
	db_create = cursor.fetchone()
	# print(db_create[1])
	out_file.write(f"\n{bytes.decode(db_create[1])};\n")
	out_file.write(f"\nUSE {db};\n")

def getTableStructure(table, db):
	query_commit("SET OPTION SQL_QUOTE_SHOW_CREATE=1")
	query(f"show create table {table}")
	tb_create = cursor.fetchone()
	# print(tb_create[1])
	out_file.write(f"\n--\n-- Table structure for table {table}\n--\n\n")
	out_file.write(f"DROP TABLE IF EXISTS {table};\n")
	out_file.write(f"{bytes.decode(tb_create[1])};\n")

	query(f"show fields from {table}")
	fields = cursor.fetchall()
	global insert_query
	insert_query = f'INSERT INTO {table} ('
	init = False
	for field in fields:
		if init == True:
			insert_query +=', '
		else:
			init = True
		insert_query += bytes.decode(field[0])
	insert_query += ") VALUES "
	return len(fields)

def dumpTable(numFields,table):
	out_file.write(f"\n--\n-- Dumping data for table {table}\n--\n")
	query(f"SELECT * FROM {table}")
	rows = cursor.fetchall()
	out_file.write(f"LOCK TABLES {table} WRITE;\n")
	insert_q = insert_query
	row_num = 0
	for row in rows:
		field_num = 0
		for field in row:
			# print(type(field),field)
			# continue
			if field_num ==0 :
				insert_q += "("
				field_num = 1
			else:
				insert_q += ", "
			if field :
				if isinstance(field,str):
					insert_q += "'"
					insert_q += field.replace('\n','\\\n')\
									.replace('\r','\\\r')\
									.replace('\\','\\\\')\
									.replace("'","\\'")\
									.replace('"','\\"')\
									.replace('\x1a','\\\x1a')\
									.replace('\x00','\\\x00')
					insert_q += "'"
				elif isinstance(field,int) or isinstance(field,float):
					#这里源码进行了一个判断，如果值为"inf", "-inf", "nan"，就写成NULL，但是MYSQL本身不是不支持这些数值吗?
					insert_q += str(field)
				elif isinstance(field,decimal.Decimal):
					insert_q += "'" + str(field) + "'"
				elif isinstance(field,bytes):
					#好像是转成十六进制，这部分还没仔细看（is_blob）
					insert_q += field.hex()
				else:
					insert_q += "NULL"
		row_num += 1
		if row_num != len(rows):
			insert_q += "),\n\t\t\t"
			out_file.write(insert_q)
			insert_q = ""
		else:
			out_file.write(insert_q)
			out_file.write(f");\n")
	out_file.write(f"UNLOCK TABLES;\n")
		
			


def dump_all_tables_in_db(db):#导出一个database中的所有表
	init_dump(db)#初始化，主要是选中这个数据库并且输出建库命令
	query("SHOW TABLES")
	tables = cursor.fetchall()
	for table in tables:
		print(table[0])
		# query_commit( f"LOCK TABLES {table[0]} READ")
	query_commit("FLUSH LOGS")
	for table in tables:
		numFields = getTableStructure(table[0], db)
		dumpTable(numFields,table[0])
	query_commit("UNLOCK TABLES")



def dump_all_databases():
	out_file.write(f"-- MySQL dump (TEST_VERSION) 0.0.1\n--\n")
	out_file.write(f"-- Host: {host}	Database: {database}\n")
	out_file.write(f"-- ------------------------------------------------------\n")
	cursor.execute("SELECT VERSION()")
	out_file.write(f"-- Database version : {cursor.fetchone()[0]} ")
	query("SHOW DATABASES")
	for db in cursor.fetchall():
		print(db[0])
		dump_all_tables_in_db(db[0])




query_commit("FLUSH TABLES");#关闭所有的表
query_commit( "FLUSH TABLES WITH READ LOCK")#再加上一个全局读锁
query_commit( "SET SESSION TRANSACTION ISOLATION " "LEVEL REPEATABLE READ") #设置会话隔离级别为REPEATABLE READ
query_commit( "START TRANSACTION " "WITH CONSISTENT SNAPSHOT")#start_transaction 开启一致性事务
query_commit("SHOW MASTER STATUS")#SHOW MASTER STATUS 获取位点。
query_commit("UNLOCK TABLES")#拿到了位点之后，需要将锁释放掉，让 Server 恢复读写，也因此，需要执行 SQL UNLOCCK TABLES。而接下来，我们就是要分情况去 dump 数据了。
dump_all_databases()



# 关闭数据库连接
out_file.close()
sql.close()