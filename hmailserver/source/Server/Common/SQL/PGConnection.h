// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#pragma once

#include <libpq-fe.h>

#include "DALConnection.h"

namespace HM
{
   class PGConnection : public DALConnection
   {
   public:
	   PGConnection(std::shared_ptr<DatabaseSettings> pSettings);
	   virtual ~PGConnection();

      virtual ConnectionResult Connect(String &sErrorMessage);
      virtual bool Disconnect();
      virtual ExecutionResult TryExecute(const SQLCommand &command, String &sErrorMessage, __int64 *iInsertID = 0, int iIgnoreErrors = 0); 
      virtual bool IsConnected() const;

      DALConnection::ExecutionResult CheckError(PGresult *pResult, const String &sAdditionalInfo, String &sOutputErrorMessage) const;
      PGconn *GetConnection() const;

      virtual bool GetSupportsCommandParameters() const {return true; }

      bool Run(const SQLCommand &command, PGresult *&result, String &sErrorMessage) const;
      // Sends the command: with its parameters bound, through PQexecParams, when it has any,
      // and through PQexec otherwise, which unlike PQexecParams runs scripts of several
      // statements. False, with the reason in sErrorMessage, if the command could not be
      // sent at all. A command that was sent can still fail; CheckError tells.
      virtual void OnConnected();

      virtual bool BeginTransaction(String &sErrorMessage);
      virtual bool CommitTransaction(String &sErrorMessage);
      virtual bool RollbackTransaction(String &sErrorMessage);
      virtual void SetTimeout(int seconds) {}

      virtual bool CheckServerVersion(String &errorMessage);

      virtual std::shared_ptr<DALRecordset> CreateRecordset();

      virtual void EscapeString(String &sInput);

      virtual std::shared_ptr<IMacroExpander> CreateMacroExpander();

   private:

  
      PGconn *dbconn_;

      bool is_connected_;
   };

}
