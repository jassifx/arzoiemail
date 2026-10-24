// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "stdafx.h"
#include "PGConnection.h"
#include "PGRecordset.h"
#include "DatabaseSettings.h"
#include "../Util/Unicode.h"
#include "Macros/PGSQLMacroExpander.h"
#include "SQLCommand.h"
#include "SQLStatement.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   PGConnection::PGConnection(std::shared_ptr<DatabaseSettings> pSettings) :
      DALConnection(pSettings),
      dbconn_(nullptr)
   {
      is_connected_ = false;
   }

   PGConnection::~PGConnection()
   {
      try
      {
         if (dbconn_)
         {
            PQfinish(dbconn_);
            dbconn_ = 0;
         }
      }
      catch (...)
      {

      }
        
   }

   DALConnection::ConnectionResult
   PGConnection::Connect(String &sErrorMessage)
   {
      try
      {

         String sUsername = database_settings_->GetUsername();
         String sPassword = database_settings_->GetPassword();
         String sServer = database_settings_->GetServer();
         String sDatabase = database_settings_->GetDatabaseName();
         long lDBPort = database_settings_->GetPort();
        
         // Keywords and values are passed separately, so that no value needs quoting. Written
         // into a connection string, a ' or a \ in the password - or any other setting - ended
         // its value early, or swallowed the next one. UTF-8, which SCRAM authentication
         // expects, rather than the system code page.
         AnsiString host, port, user, password, database;
         if (!Unicode::WideToMultiByte(sServer, host) ||
             !Unicode::WideToMultiByte(StringParser::IntToString((int) lDBPort), port) ||
             !Unicode::WideToMultiByte(sUsername, user) ||
             !Unicode::WideToMultiByte(sPassword, password) ||
             !Unicode::WideToMultiByte(sDatabase.IsEmpty() ? String(_T("postgres")) : sDatabase, database))
         {
            // Retrying would not change the outcome.
            sErrorMessage = "Could not convert the database settings into UTF-8.";
            return FatalError;
         }

         const char *keywords[] = { "host", "port", "user", "password", "dbname", nullptr };
         const char *values[] = { host.c_str(), port.c_str(), user.c_str(), password.c_str(), database.c_str(), nullptr };

         dbconn_ = PQconnectdbParams(keywords, values, 0);

        
         if (PQstatus(dbconn_) != CONNECTION_OK)
         {
            sErrorMessage = PQerrorMessage(dbconn_);
            return TemporaryFailure;
         }

         is_connected_ = true;
      }
      catch (...)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5085, "PGConnection::Connect", "An unhandled error occurred when connecting to the database");

         return TemporaryFailure;
      }
          
      return Connected;
   }

   bool
   PGConnection::Disconnect()
   {
      if (dbconn_)
      {
         PQfinish(dbconn_);
         dbconn_ = 0;
      }

      return true;
   }

   DALConnection::ExecutionResult
   PGConnection::TryExecute(const SQLCommand &command, String &sErrorMessage, __int64 *iInsertID, int iIgnoreErrors) 
   {
      String SQL = command.GetQueryString();

      try
      {
         PGresult *pResult = nullptr;
         String runError;
         if (!Run(command, pResult, runError))
         {
            ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5106, "PGConnection::TryExecute", runError);
            sErrorMessage = runError;
            return DALConnection::DALUnknown;
         }

         bool bIgnoreErrors = SQL.Find(_T("[IGNORE-ERRORS]")) >= 0;

         if (!bIgnoreErrors)
         {
            DALConnection::ExecutionResult result = CheckError(pResult, SQL, sErrorMessage);
            if (result != DALSuccess)
            {
               if (pResult != 0)
                  PQclear(pResult);

               return result;
            }
         }

         ExecStatusType iExecResult = PQresultStatus(pResult);
         
         // Check if a value has been returned. Will only occur if we've
         // inserted a value.
         if (iInsertID != nullptr && iExecResult == PGRES_TUPLES_OK)
         {
            // pick the ID from the first row.
            char *pRetVal = PQgetvalue(pResult, 0, 0);
            *iInsertID = pRetVal ? _atoi64(pRetVal) : 0;
         }

         if (pResult != 0)
            PQclear(pResult);
        
      }
      catch (...)
      {
         sErrorMessage = "Source: PGConnection::TryExecute, Code: HM5084, Description: An unhanded error occurred while executing: " + SQL;
         return DALConnection::DALUnknown;
      }

      return DALConnection::DALSuccess;
   }

   namespace
   {
      // From the PostgreSQL catalog (pg_type.dat). libpq-fe.h does not carry them.
      const Oid PG_INT4_OID = 23;
      const Oid PG_INT8_OID = 20;

      // A string parameter is sent with no type, so that the server takes the type the
      // context gives it - a varchar column, a timestamp - as it did with the literals
      // written before.
      const Oid PG_UNTYPED = 0;
   }

   bool
   PGConnection::Run(const SQLCommand &command, PGresult *&result, String &sErrorMessage) const
   {
      result = nullptr;

      if (command.GetParameters().empty())
      {
         AnsiString query;
         if (!Unicode::WideToMultiByte(command.GetQueryString(), query))
         {
            sErrorMessage = _T("Could not convert the query into UTF-8: ") + command.GetQueryString();
            return false;
         }

         result = PQexec(dbconn_, query);
         return true;
      }

      String positionalQuery;
      std::vector<const SQLParameter *> parameters;
      if (!SQLStatement::ToPositionalParameters(command, positionalQuery, parameters))
      {
         sErrorMessage = _T("A parameter of the query stands inside a literal, where it cannot be bound: ") + command.GetQueryString();
         return false;
      }

      AnsiString query;
      if (!Unicode::WideToMultiByte(positionalQuery, query))
      {
         sErrorMessage = _T("Could not convert the query into UTF-8: ") + command.GetQueryString();
         return false;
      }

      // Every value is sent as text, the format libpq takes by default, together with the
      // type of the number parameters.
      std::vector<AnsiString> values(parameters.size());
      std::vector<Oid> types(parameters.size());

      for (size_t i = 0; i < parameters.size(); i++)
      {
         const SQLParameter &parameter = *parameters[i];

         switch (parameter.GetType())
         {
         case SQLParameter::ParamTypeInt32:
            types[i] = PG_INT4_OID;
            values[i] = AnsiString(StringParser::IntToString(parameter.GetInt32Value()));
            break;
         case SQLParameter::ParamTypeInt64:
            types[i] = PG_INT8_OID;
            values[i] = AnsiString(StringParser::IntToString(parameter.GetInt64Value()));
            break;
         case SQLParameter::ParamTypeUnsignedInt32:
            // Above 2^31 - 1, which int4 cannot hold.
            types[i] = PG_INT8_OID;
            values[i] = AnsiString(StringParser::IntToString(parameter.GetUnsignedInt32Value()));
            break;
         case SQLParameter::ParamTypeString:
            types[i] = PG_UNTYPED;
            if (!Unicode::WideToMultiByte(parameter.GetStringValue(), values[i]))
            {
               sErrorMessage = _T("Could not convert the value of ") + parameter.GetName() + _T(" into UTF-8.");
               return false;
            }
            break;
         default:
            sErrorMessage = _T("The parameter ") + parameter.GetName() + _T(" has a type that cannot be bound.");
            return false;
         }
      }

      // Taken once the values are complete, so that no pointer outlives a reallocation.
      std::vector<const char *> valuePointers(parameters.size());
      for (size_t i = 0; i < parameters.size(); i++)
         valuePointers[i] = values[i].c_str();

      result = PQexecParams(dbconn_, query, (int) parameters.size(), types.data(), valuePointers.data(),
                            nullptr, nullptr, 0);
      return true;
   }

   bool
   PGConnection::IsConnected() const
   {
      return is_connected_;
   }

   PGconn*
   PGConnection::GetConnection() const
   {
      return dbconn_;
   }

   DALConnection::ExecutionResult
   PGConnection::CheckError(PGresult *pResult, const String &sAdditionalInfo, String &sOutputErrorMessage) const
   {
      try
      {
         String sErrorMsg = "";

         DALConnection::ExecutionResult result = DALConnection::DALUnknown;
         
         if (pResult)
         {  
            ExecStatusType iExecResult = PQresultStatus(pResult);

            if (iExecResult == PGRES_COMMAND_OK || iExecResult == PGRES_TUPLES_OK) 
            {
               result = DALConnection::DALSuccess;
               return result;
            }
            else if (iExecResult == PGRES_FATAL_ERROR)
            {
               result = DALConnection::DALErrorInSQL;
            }

            // Retrieve error message
            sErrorMsg  = PQresultErrorMessage(pResult);
         }
         else
         {
            sErrorMsg  = PQerrorMessage(dbconn_);

            if (sErrorMsg.IsEmpty())
               sErrorMsg = "Unknown error. Error structure not initialized.";

            result = DALConnection::DALConnectionProblem;
         }

         String sErrorMessage;
         sErrorMessage.Format(_T("Postgres: %s (Additional info: %s)"), sErrorMsg.c_str(), sAdditionalInfo);

         sOutputErrorMessage = sErrorMessage;

         return result;
      }
      catch (...)
      {
         ErrorManager::Instance()->ReportError(HM::ErrorManager::High, 5083, "PGConnection::CheckError", "An unhandled error occurred while checking for errors.");

         return DALConnection::DALUnknown;
      }
   }

   void 
   PGConnection::OnConnected()
   //---------------------------------------------------------------------------()
   // DESCRIPTION:
   // Not much going on here...
   //---------------------------------------------------------------------------()
   {
      
   }

   bool 
   PGConnection::BeginTransaction(String &sErrorMessage)
   {
      return TryExecute(SQLCommand("BEGIN TRANSACTION"), sErrorMessage, 0, 0) == DALSuccess;
   }

   bool 
   PGConnection::CommitTransaction(String &sErrorMessage)
   {
      return TryExecute(SQLCommand("COMMIT TRANSACTION"), sErrorMessage, 0, 0) == DALSuccess;
   }

   bool 
   PGConnection::RollbackTransaction(String &sErrorMessage)
   {
      return TryExecute(SQLCommand("ROLLBACK TRANSACTION"), sErrorMessage, 0, 0) == DALSuccess;
   }

   bool 
   PGConnection::CheckServerVersion(String &errorMessage)
   {

      return true;
   }

   std::shared_ptr<DALRecordset> 
   PGConnection::CreateRecordset()
   {
      std::shared_ptr<PGRecordset> recordset = std::shared_ptr<PGRecordset>(new PGRecordset());
      return recordset;
   }

   void
   PGConnection::EscapeString(String &sInput)
   {
      sInput.Replace(_T("'"), _T("''"));
      sInput.Replace(_T("\\"), _T("\\\\"));
   }

   std::shared_ptr<IMacroExpander> 
   PGConnection::CreateMacroExpander()
   {
      std::shared_ptr<PGSQLMacroExpander> expander = std::shared_ptr<PGSQLMacroExpander>(new PGSQLMacroExpander());
      return expander;
   }

}