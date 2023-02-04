#pragma once
class database
{
public:
	database();
	~database();
	BOOL CreateDatabase();
	BOOL CompactDatabase();
	BOOL CreateTable();
	TCHAR m_szDatabaseFilePath[MAX_PATH];
	BOOL SQLExecute(LPCTSTR lpszSQL);
};
