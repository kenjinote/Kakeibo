#import "C:\Program Files (x86)\Common Files\Microsoft Shared\DAO\dao360.dll" rename_namespace("DAO") rename("EOF", "adoEOF")
#import "C:\Program Files (x86)\Common Files\System\ado\msado60.tlb" no_namespace rename("EOF", "EndOfFile")

#include <windows.h>
#include <shlwapi.h>
#include <odbcinst.h>
#include "database.h"

database::database()
{
	TCHAR szModuleFilePath[MAX_PATH];
	GetModuleFileName(GetModuleHandle(0), szModuleFilePath, MAX_PATH);
	WIN32_FIND_DATA FindFileData;
	const HANDLE hFind = FindFirstFile(szModuleFilePath, &FindFileData);
	if (hFind != INVALID_HANDLE_VALUE)
	{
		FindClose(hFind);
		szModuleFilePath[lstrlen(szModuleFilePath) - lstrlen(FindFileData.cFileName)] = 0;
	}
	lstrcpy(m_szDatabaseFilePath, szModuleFilePath);
	PathAppend(m_szDatabaseFilePath, TEXT("kakeibo.mdb"));
	//if (!PathFileExists(m_szDatabaseFilePath))
	{
		// DB���Ȃ��Ƃ��͍��
		//if (CreateDatabase())
		{
			CreateTable();
		}
	}
}

database::~database()
{
	//DeleteFile(m_szDatabaseFilePath);
}

BOOL database::CreateDatabase()
{
	if (PathFileExists(m_szDatabaseFilePath))
	{
		if (!DeleteFile(m_szDatabaseFilePath))
		{
			return FALSE;
		}
	}
	TCHAR szAttributes[1024];
	wsprintf(szAttributes, TEXT("CREATE_DB=\"%s\" General\0"), m_szDatabaseFilePath);
	return SQLConfigDataSource(0, ODBC_ADD_DSN, TEXT("Microsoft Access Driver (*.mdb)"), szAttributes);
}

BOOL database::CompactDatabase()
{
	if (!PathFileExists(m_szDatabaseFilePath))
	{
		return FALSE;
	}
	TCHAR szAttributes[1024];
	wsprintf(szAttributes, TEXT("COMPACT_DB=\"%s\" \"%s\" General\0"), m_szDatabaseFilePath, m_szDatabaseFilePath);
	return SQLConfigDataSource(0, ODBC_ADD_DSN, TEXT("Microsoft Access Driver (*.mdb)"), szAttributes);
}

BOOL database::CreateTable()
{
	SQLExecute(TEXT("CREATE TABLE ����(���O VARCHAR (255), ���Z VARCHAR (255));"));
	SQLExecute(TEXT("INSERT INTO ����(���O,���Z)VALUES('�R�c���Y','�X�C�J����');"));
	SQLExecute(TEXT("INSERT INTO ����(���O,���Z)VALUES('�R�c�Ԏq','�������t');"));

//	SQLExecute(TEXT("CREATE TABLE tblDate(���t DATETIME, ���� int NOT NULL, ���i int NOT NULL);"));
//	SQLExecute(TEXT("INSERT INTO tblDate(���t,����,���i)VALUES('2015/10/10',0,5000);"));
	//CompactDatabase();
	return TRUE;
}

BOOL database::SQLExecute(LPCTSTR lpszSQL)
{
	HRESULT hr;
	_ConnectionPtr pCon(NULL);
	hr = pCon.CreateInstance(__uuidof(Connection));
	if (FAILED(hr))
	{
		return FALSE;
	}
	TCHAR szString[1024];
	wsprintf(szString, TEXT("Provider=Microsoft.Jet.OLEDB.4.0;Data Source=\"%s\";"), m_szDatabaseFilePath);
	hr = pCon->Open(szString, _bstr_t(""), _bstr_t(""), adOpenUnspecified);
	if (FAILED(hr))
	{
		return FALSE;
	}
	BOOL bRet = TRUE;
	try
	{
		_CommandPtr pCommand(NULL);
		pCommand.CreateInstance(__uuidof(Command));
		pCommand->ActiveConnection = pCon;
		pCommand->CommandText = lpszSQL;
		pCommand->Execute(NULL, NULL, adCmdText);
	}
	catch (_com_error&e)
	{
		OutputDebugString(e.Description());
		bRet = FALSE;
	}
	pCon->Close();
	pCon.Release();
	pCon = NULL;
	return TRUE;
}