#include "pch.h"
#include "framework.h"
#include "DatabaseTest.h"
#include "DatabaseService.h"
#include "DatabaseConfig.h"
#include <cstdlib>

void RunDatabaseConnectionTest()
{
    char* dbPassword = nullptr;
    size_t passwordLength = 0;

    errno_t result = _dupenv_s(
        &dbPassword,
        &passwordLength,
        "CHATPROJ_DB_PASSWORD"
    );

    if (result != 0 || dbPassword == nullptr)
    {
        AfxMessageBox(
            L"CHATPROJ_DB_PASSWORD environment variable not found.",
            MB_ICONERROR
        );
        return;
    }

    bool connected = DatabaseService::Instance().Connect(
        DatabaseConfig::HOST,
        DatabaseConfig::USERNAME,
        dbPassword,
        DatabaseConfig::DATABASE,
        DatabaseConfig::PORT
    );

    free(dbPassword);

    if (connected)
    {
        AfxMessageBox(
            L"MySQL connection successful!",
            MB_ICONINFORMATION
        );
        DatabaseService::Instance().Disconnect();
    }
    else
    {
        std::string error =
            DatabaseService::Instance().GetLastError();

        CString message;
        message.Format(L"MySQL connection failed.\n\nError: %S",
            error.c_str());

        AfxMessageBox(message, MB_ICONERROR);
    }
}