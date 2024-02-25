#include <mysql/mysql.h>
#include <stdio.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <list>
#include <pthread.h>
#include <iostream>
#include "sql_connection_pool.h"

using namespace std;
// 构造函数，初始化当前已使用连接数和空闲连接数
connection_pool::connection_pool()
{
	m_CurConn = 0;
	m_FreeConn = 0;
}

connection_pool *connection_pool::GetInstance()
{
	static connection_pool connPool;
	return &connPool;
}

//构造初始化
void connection_pool::init(string url, string User, string PassWord, string DBName, int Port, int MaxConn, int close_log)
{
	// 初始化数据库信息
	m_url = url;			 // 主机地址
	m_Port = Port;			 // 数据库端
	m_User = User;			 // 登录数据
	m_PassWord = PassWord;	 // 登录数据
	m_DatabaseName = DBName; // 使用数据
	m_close_log = close_log; // 日志开关

	// 创建MaxConn条数据库连接
	for (int i = 0; i < MaxConn; i++)
	{
		MYSQL *con = NULL;
		con = mysql_init(con);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 连接数据库
		con = mysql_real_connect(con, url.c_str(), User.c_str(), PassWord.c_str(), DBName.c_str(), Port, NULL, 0);

		if (con == NULL)
		{
			LOG_ERROR("MySQL Error");
			exit(1);
		}
		// 更新连接池和空闲连接数量
		connList.push_back(con);
		++m_FreeConn;
	}
	// 将信号量初始化为最大连接次数
	reserve = sem(m_FreeConn);

	m_MaxConn = m_FreeConn;
}

// 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
MYSQL *connection_pool::GetConnection()
{
	MYSQL *con = NULL;
	// 无可用连接
	if (0 == connList.size())
		return NULL;
	// P操作
	reserve.wait();
	// 加锁
	lock.lock();
	// 取连接池第一个连接
	con = connList.front();
	// 删除连接池第一个连接
	connList.pop_front();
	// 更新空闲连接个数和已连接数，未使用
	--m_FreeConn;
	++m_CurConn;
	// 解锁
	lock.unlock();
	return con;
}

// 释放当前使用的连接
bool connection_pool::ReleaseConnection(MYSQL *con)
{
	if (NULL == con)
		return false;

	lock.lock();
	// 更新连接池
	connList.push_back(con);
	++m_FreeConn;
	--m_CurConn;

	lock.unlock();
	// V操作
	reserve.post();
	return true;
}

// 销毁数据库连接池
void connection_pool::DestroyPool()
{

	lock.lock();
	if (connList.size() > 0)
	{
		// 逐个连接进行关闭
		list<MYSQL *>::iterator it;
		for (it = connList.begin(); it != connList.end(); ++it)
		{
			MYSQL *con = *it;
			mysql_close(con);
		}
		m_CurConn = 0;
		m_FreeConn = 0;

		// 清空list
		connList.clear();
	}

	lock.unlock();
}

//当前空闲的连接数
int connection_pool::GetFreeConn()
{
	return this->m_FreeConn;
}

connection_pool::~connection_pool()
{
	DestroyPool();
}

// RAII机制取一个可用连接
connectionRAII::connectionRAII(MYSQL **SQL, connection_pool *connPool)
{
	// 取一个可用连接
	*SQL = connPool->GetConnection();

	conRAII = *SQL;		 // 记录分配的可用连接
	poolRAII = connPool; // 记录对象
}

connectionRAII::~connectionRAII(){
	poolRAII->ReleaseConnection(conRAII);
}