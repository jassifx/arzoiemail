// Copyright (c) 2010 Martin Knafve / hMailServer.com.  
// http://www.hmailserver.com

#include "stdafx.h"
#include "SQLStatement.h"
#include "SQLCommand.h"

#include "DatabaseSettings.h"
#include "../Util/VariantDateTime.h"
#include "../Util/Time.h"

#ifdef _DEBUG
#define DEBUG_NEW new(_NORMAL_BLOCK, __FILE__, __LINE__)
#define new DEBUG_NEW
#endif

namespace HM
{
   SQLStatement::SQLStatement() :
      type_(STUndefined),
      top_rows_(-1)
   {

   }

   SQLStatement::SQLStatement(eStatementType iType, const String &tableName) :
      type_(iType),
      top_rows_(-1),
      table_(tableName)
   {

   }


   SQLStatement::~SQLStatement()
   {

   }

   void 
   SQLStatement::AddColumn(const String &sName, const String &sValue)
   {
      Column p;

      p.sName = sName;
      p.iType = ColTypeString;
      p.sString = sValue;

      vecColumns.push_back(p);
   }

   void 
   SQLStatement::AddColumn(const String &sName, const String &sValue, int iMaxLength)
   {
      Column p;

      String sCopyOfValue = sValue;

      if (sCopyOfValue.GetLength() > iMaxLength)
         sCopyOfValue = sCopyOfValue.Mid(0, iMaxLength);

      p.sName = sName;
      p.iType = ColTypeString;
      p.sString = sCopyOfValue;

      vecColumns.push_back(p);
   }

   void 
   SQLStatement::AddColumnDate(const String &sName, const DateTime & dtValue)
   {
      String value = Time::GetTimeStampFromDateTime(dtValue);

      Column p;

      p.sName = sName;

      // If the date is older than 1800, don't store it. This is to solve
      // limitations in SQL Server.
      if (dtValue.GetStatus() == DateTime::invalid || dtValue.GetYear() < 1800)
      {
         p.iType = ColTypeRaw;
         p.sString = "NULL";
      }
      else
      {
         p.iType = ColTypeString;
         p.sString = value;
      }

      vecColumns.push_back(p);
   }

   void 
   SQLStatement::AddColumnNULL(const String &sName)
   {
      Column p;

      p.sName = sName;
      p.iType = ColTypeRaw;
      p.sString = "NULL";

      vecColumns.push_back(p);
   }



   void 
   SQLStatement::AddColumn(const String &sName, long lValue)
   {
      Column p;
      p.sName = sName;
      p.iType = ColTypeValue;
      p.iInt = lValue;

      vecColumns.push_back(p);
   }

   void 
   SQLStatement::AddColumnInt64(const String &sName, __int64 lValue)
   {
      Column p;
      p.sName = sName;
      p.iType = ColTypeValue;
      p.iInt = lValue;

      vecColumns.push_back(p);
   }

   void 
   SQLStatement::AddColumn(const String & sName)
   {
      Column p;
      p.sName = sName;
      p.iType = ColTypeUnknown;
   
      vecColumns.push_back(p);
   }

   void
   SQLStatement::AddColumnCommand(const String &column, const String &command)
   {
      Column p;
      p.sName = column;
      p.iType = ColTypeRaw;
      p.sString = command;

      vecColumns.push_back(p);
   }

   SQLCommand
   SQLStatement::GetCommand() const
   {
      DatabaseSettings::SQLDBType dbType = IniFileSettings::Instance()->GetDatabaseType();
      if (dbType == DatabaseSettings::TypeUnknown)
      {
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetCommand()", Formatter::Format("Unknown database type: {0}", dbType));
         SQLCommand emtpy;
         return emtpy;
      }

      String sSQL;
      SQLCommand command;

      int parameterValue = 1;
      if (type_ == SQLStatement::STInsert)
      {
         sSQL.append(_T("INSERT INTO "));
         sSQL.append(table_);
         sSQL.append(_T(" "));

         // First add columns
         sSQL.append(_T("("));
         bool first = true;
         for(Column c : vecColumns)
         {
            if (!first)
               sSQL.append(_T(", "));
            else
               first = false;

            sSQL.append(c.sName);
         }
         sSQL.append(_T(") VALUES ("));

         // Now append values
         first = true;
         for(Column c : vecColumns)
         {
            if (!first)
               sSQL += ", ";
            else
               first = false;

            parameterValue++;
            String parameterName = "@" + c.sName + StringParser::IntToString(parameterValue);

            switch (c.iType)
            {
            case ColTypeValue:
               sSQL.append(parameterName);
               command.AddParameter(parameterName, c.iInt);
               break;
            case ColTypeString:
               sSQL.append(parameterName);
               command.AddParameter(parameterName, c.sString);
               break;
            case ColTypeRaw:
               sSQL.append(Escape(c.sString));
               break;
            }
         }

         sSQL.append(_T(")"));

         if (dbType == DatabaseSettings::TypePGServer && !identity_column_.IsEmpty())
         {
            sSQL += " RETURNING " + identity_column_;
         }

      }
      else if (type_ == SQLStatement::STUpdate)
      {
         sSQL.append(_T("UPDATE "));
         sSQL.append(table_);
         sSQL.append(_T(" SET "));

         // First add columns
         bool first = true;
         for(Column c : vecColumns)
         {
            if (!first)
               sSQL.append(_T(", "));
            else
               first = false;

            parameterValue++;
            String parameterName = "@" + c.sName +  StringParser::IntToString(parameterValue);

            sSQL.append(c.sName);
            sSQL.append(_T("="));

            switch (c.iType)
            {
            case ColTypeValue:
               sSQL.append(parameterName);
               command.AddParameter(parameterName, c.iInt);
               break;
            case ColTypeString:
               sSQL.append(parameterName);
               command.AddParameter(parameterName, c.sString);
               break;
            case ColTypeRaw:
               sSQL.append(Escape(c.sString));
               break;
            }
         }
      }
      else if (type_ == SQLStatement::STDelete)
      {
         sSQL = "DELETE FROM ";
         sSQL.append(table_);
      }
      else if (type_ == SQLStatement::STSelect)
      {
         sSQL = "SELECT ";

         if (top_rows_ > -1)
         {
            String value;

            switch (dbType)
            {
            case DatabaseSettings::TypeMSSQLServer:
               // SQL Server 2000 does not support ( and ) around the value.
               value.Format(_T(" TOP %d "), top_rows_);
               sSQL.append(value);
               break;
            case DatabaseSettings::TypeMSSQLCompactEdition:
               // SQL Server Compact Edition 3.5 requires () around the value.
               value.Format(_T(" TOP (%d) "), top_rows_);
               sSQL.append(value);
               break;
            }
         }

         if (vecColumns.size() == 0)
         {
            sSQL += " * ";
         }
         else
         {
            std::vector<Column>::const_iterator it = vecColumns.begin();
            std::vector<Column>::const_iterator itEnd = vecColumns.end();

            bool first = true;

            for(Column c : vecColumns)
            {
               if (!first)
                  sSQL.append(_T(", "));
               else
                  first = false;
               
               sSQL.append(c.sName);
            }
         }

         sSQL.append(_T(" FROM "));
         sSQL.append(table_);

         if (!additional_sql_.IsEmpty())
            sSQL.append(_T(" ") + additional_sql_);
      }

      if (where_clause_columns_.size() != 0)
      {
         sSQL.append(_T(" WHERE "));
         bool first = true;
         for(Column col : where_clause_columns_)
         {
            if (!first)
               sSQL.append(_T(" AND "));
            else
               first = false;

            parameterValue++;
            String parameterName = "@" + col.sName + StringParser::IntToString(parameterValue);

            if (col.iType == ColTypeString)
            {
               switch (dbType)
               {
               case DatabaseSettings::TypeMYSQLServer:
               case DatabaseSettings::TypeMSSQLServer:
               case DatabaseSettings::TypeMSSQLCompactEdition:
                  sSQL += col.sName + " = " + parameterName;
                  break;
               case DatabaseSettings::TypePGServer:
                  sSQL += "lower(" + col.sName + ") = lower(" + parameterName + ")";
                  break;
               }

               command.AddParameter(parameterName, col.sString);
            }
            else if (col.iType == ColTypeValue)
            {
               sSQL += col.sName + " = " + parameterName;
               command.AddParameter(parameterName, col.iInt);
            }
            else if (col.iType == ColTypeRaw)
            {
               sSQL += col.sName + " = " + Escape(col.sString);
            }
         }
      }
      else if (!where_.IsEmpty())
      {
         sSQL.append(_T(" WHERE "));
         sSQL.append(where_);
      }

      if (type_ == SQLStatement::STSelect)
      {
         if (top_rows_ > -1)
         {
            HM::DatabaseSettings::SQLDBType DBType = IniFileSettings::Instance()->GetDatabaseType();
            String value;
            switch (DBType)
            {
            case DatabaseSettings::TypePGServer:
               value.Format(_T(" LIMIT %d "), top_rows_);
               sSQL.append(value);
               break;
            case DatabaseSettings::TypeMYSQLServer:
               value.Format(_T(" LIMIT 0, %d "), top_rows_);
               sSQL.append(value);
               break;
            }
         }
      }
   
      command.SetQueryString(sSQL);
      return command;
   }

   void 
   SQLStatement::AddWhereClauseColumn(const String &sName, const String &sValue)
   {
      Column col;
      col.iType = ColTypeString;
      col.sName = sName;
      col.sString = sValue;

      where_clause_columns_.push_back(col);
   }

   void
   SQLStatement::SetTopRows(int rowCount)
   {
      top_rows_ = rowCount;
   }

   String 
   SQLStatement::GetCreateDatabase(std::shared_ptr<DatabaseSettings> pSettings, const String &sDatabaseName)
   {
      HM::DatabaseSettings::SQLDBType DBType = pSettings->GetType();
      String sSQL;
      switch (DBType)
      {
      case DatabaseSettings::TypeMSSQLServer:
         sSQL.Format(_T("create database %s"), sDatabaseName.c_str());
         break;
      case DatabaseSettings::TypeMYSQLServer:
         sSQL.Format(_T("create database %s character set 'utf8'"), sDatabaseName.c_str());
         break;
      case DatabaseSettings::TypePGServer:
         sSQL.Format(_T("create database \"%s\" ENCODING = 'UTF8'"), sDatabaseName.c_str());
         break;
      default:
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetCreateDatabase()", Formatter::Format("Unknown database type: {0}", DBType));
         break;
      }

      return sSQL;
   }

   int 
   SQLStatement::GetNoOfCols() const
   { 
      return (int) vecColumns.size(); 
   }

   String 
   SQLStatement::GetCurrentTimestamp()
   {
      HM::DatabaseSettings::SQLDBType DBType = IniFileSettings::Instance()->GetDatabaseType();
      
      switch (DBType)
      {
      case DatabaseSettings::TypeMSSQLServer:
      case DatabaseSettings::TypeMSSQLCompactEdition:
         return "GETDATE()";
      case DatabaseSettings::TypePGServer:
         return "current_timestamp";
      case DatabaseSettings::TypeMYSQLServer:
         return "NOW()";
      default:
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetCurrentTimestamp()", Formatter::Format("Unknown database type: {0}", DBType));   
      }

      assert(0);
      return "";
   }

   String 
   SQLStatement::GetLeftFunction(const String &sParamName, int iLength)
   {
      HM::DatabaseSettings::SQLDBType DBType = IniFileSettings::Instance()->GetDatabaseType();
      String sRetVal;

      switch (DBType)
      {
      case DatabaseSettings::TypeMSSQLServer:
      case DatabaseSettings::TypeMSSQLCompactEdition:
         /*
            Use SUBSTRING instead of the normal LEFT. LEFT doesn't work
            with MSSQL Compact Edition while SUBSTRING works with both.
         */
         sRetVal.Format(_T("SUBSTRING(%s, 1, %d)"), sParamName.c_str(), iLength);
         break;
      case DatabaseSettings::TypePGServer:
         sRetVal.Format(_T("SUBSTRING(%s FROM 1 FOR %d)"), sParamName.c_str(), iLength);
         break;
      case DatabaseSettings::TypeMYSQLServer:
         sRetVal.Format(_T("LEFT(%s, %d)"), sParamName.c_str(), iLength);
         break;
      default:
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetLeftFunction()", Formatter::Format("Unknown database type: {0}", DBType));
         break;
      }

      return sRetVal;
   }

   String 
   SQLStatement::GetTopRows(const String &tableName, int rows)
   {
      HM::DatabaseSettings::SQLDBType DBType = IniFileSettings::Instance()->GetDatabaseType();
      String sRetVal;

      switch (DBType)
      {
      case DatabaseSettings::TypeMSSQLServer:
      case DatabaseSettings::TypeMSSQLCompactEdition:
         /*
         Use SUBSTRING instead of the normal LEFT. LEFT doesn't work
         with MSSQL Compact Edition while SUBSTRING works with both.
         */
         sRetVal.Format(_T("SELECT TOP %d FROM %s"), rows, tableName.c_str());
         break;
      case DatabaseSettings::TypePGServer:
         sRetVal.Format(_T("SELECT * FROM %s LIMIT %d"), tableName.c_str(), rows);
         break;
      case DatabaseSettings::TypeMYSQLServer:
         sRetVal.Format(_T("SELECT * FROM %s LIMIT 0, %d"), tableName.c_str(), rows);
         break;
      default:
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetTopRows()", Formatter::Format("Unknown database type: {0}", DBType));
         break;
      }

      return sRetVal;
   }

   String 
   SQLStatement::GetCurrentTimestampPlusMinutes(int iMinutes)
   {
      HM::DatabaseSettings::SQLDBType DBType = IniFileSettings::Instance()->GetDatabaseType();

      String sRetVal;

      switch (DBType)
      {
      case DatabaseSettings::TypeMYSQLServer:
         sRetVal.Format(_T("DATE_ADD(CONCAT(CURDATE(), ' ', CURTIME()), INTERVAL %d MINUTE)"), iMinutes);
         break;
      case DatabaseSettings::TypeMSSQLServer:
      case DatabaseSettings::TypeMSSQLCompactEdition:
         sRetVal.Format(_T("DATEADD(mi, %d, GETDATE())"), iMinutes);
         break;
      case DatabaseSettings::TypePGServer:
         sRetVal.Format(_T("current_timestamp + INTERVAL '%d minutes'"), iMinutes);
         break;
      default:
         ErrorManager::Instance()->ReportError(ErrorManager::Critical, 5407, "SQLStatement::GetCurrentTimestampPlusMinutes()", Formatter::Format("Unknown database type: {0}", DBType));
         break;

      }

      return sRetVal;
   }

   String 
   SQLStatement::Escape(const String &input)
   {
      String sRetVal = input;

      sRetVal.Replace(_T("'"), _T("''"));

      HM::DatabaseSettings::SQLDBType iType = IniFileSettings::Instance()->GetDatabaseType();

      if (iType == DatabaseSettings::TypeMYSQLServer || iType == DatabaseSettings::TypePGServer)
         sRetVal.Replace(_T("\\"), _T("\\\\"));

      return sRetVal;
   }

   namespace
   {
      bool IsParameterNameCharacter(wchar_t c)
      {
         return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
      }
   }

   String
   SQLStatement::GenerateFromCommand(const SQLCommand &command)
   {
      // Used for the database clients that take no command parameters (MySQL, PostgreSQL),
      // so the values are written into the query as literals.
      //
      // The query is scanned once, as written, and every parameter name is replaced where
      // it stands. A value that has been written into the query is never scanned again.
      // Replacing one parameter name at a time across the whole query would also replace
      // it inside the values already written: a sender address "x@RECIPIENTADDRESS" would
      // have the recipient written into the middle of it, quotes and all, and the quotes
      // would end the literal and leave the rest of the recipient as SQL.
      //
      // A name is matched as a whole token, @ followed by letters, digits and underscores,
      // so @ID never matches the start of @IDX. Anything else, such as @@IDENTITY or an
      // @ in the query text that names no parameter, is copied unchanged.
      const String queryString = command.GetQueryString();
      const std::list<SQLParameter> &parameters = command.GetParameters();

      // Indexed through std::wstring, whose operator[] takes a size_t. String overloads it
      // for int and other types, which a size_t index makes ambiguous.
      const std::wstring &query = queryString;

      String result;
      result.reserve(query.size());

      size_t position = 0;
      while (position < query.size())
      {
         if (query[position] != '@')
         {
            result += query[position];
            position++;
            continue;
         }

         size_t end = position + 1;
         while (end < query.size() && IsParameterNameCharacter(query[end]))
            end++;

         const String token = query.substr(position, end - position);

         auto parameter = std::find_if(parameters.begin(), parameters.end(),
            [&token](const SQLParameter &candidate) { return candidate.GetName() == token; });

         if (end == position + 1 || parameter == parameters.end())
            result += token;
         else
         {
            switch (parameter->GetType())
            {
            case SQLParameter::ParamTypeInt32:
               result += StringParser::IntToString(parameter->GetInt32Value());
               break;
            case SQLParameter::ParamTypeInt64:
               result += StringParser::IntToString(parameter->GetInt64Value());
               break;
            case SQLParameter::ParamTypeUnsignedInt32:
               result += StringParser::IntToString(parameter->GetUnsignedInt32Value());
               break;
            case SQLParameter::ParamTypeString:
               result += _T("'") + Escape(parameter->GetStringValue()) + _T("'");
               break;
            default:
               // Left as it is, so that the query fails rather than runs with a value this
               // function does not know how to write.
               result += token;
               break;
            }
         }

         position = end;
      }

      return result;
   }

   bool
   SQLStatement::ToPositionalParameters(const SQLCommand &command, String &positionalQuery,
                                        std::vector<const SQLParameter *> &orderedParameters)
   {
      // For the database clients that bind parameters by position (PostgreSQL's $1, $2, ...).
      // The query is scanned once, as GenerateFromCommand does, and every parameter name is
      // replaced by the position of its parameter. A parameter used twice gets the same
      // position both times; the positions follow the order the names first appear in.
      //
      // A bound parameter is a value, so it cannot stand inside a literal of the query, where
      // the server would read $1 as the two characters. Such a query is refused: false, and
      // the caller should not run it.
      const String queryString = command.GetQueryString();
      const std::list<SQLParameter> &parameters = command.GetParameters();

      const std::wstring &query = queryString;

      positionalQuery.clear();
      positionalQuery.reserve(query.size());
      orderedParameters.clear();

      bool insideLiteral = false;

      size_t position = 0;
      while (position < query.size())
      {
         wchar_t c = query[position];

         // '' inside a literal is an escaped quote: it ends the literal and starts it again,
         // which leaves the state as it was.
         if (c == '\'')
            insideLiteral = !insideLiteral;

         if (c != '@')
         {
            positionalQuery += c;
            position++;
            continue;
         }

         size_t end = position + 1;
         while (end < query.size() && IsParameterNameCharacter(query[end]))
            end++;

         const String token = query.substr(position, end - position);

         auto parameter = std::find_if(parameters.begin(), parameters.end(),
            [&token](const SQLParameter &candidate) { return candidate.GetName() == token; });

         if (end == position + 1 || parameter == parameters.end())
            positionalQuery += token;
         else
         {
            if (insideLiteral)
               return false;

            auto bound = std::find(orderedParameters.begin(), orderedParameters.end(), &*parameter);
            size_t index = bound - orderedParameters.begin();
            if (bound == orderedParameters.end())
               orderedParameters.push_back(&*parameter);

            positionalQuery += _T("$") + StringParser::IntToString((int) index + 1);
         }

         position = end;
      }

      return true;
   }

   String
   SQLStatement::ConvertWildcardToLike(String input)
   {
      input.Replace(_T("/"), _T("//"));
      input.Replace(_T("%"), _T("/%"));
      input.Replace(_T("_"), _T("/_"));
      input.Replace(_T("?"), _T("_"));
      input.Replace(_T("*"), _T("%"));
      return input;
   }

   String
   SQLStatement::ConvertLikeToWildcard(String input)
   {
      input.Replace(_T("//"), _T("/"));
      input.Replace(_T("/%"), _T("¤¤¤ESCAPED¤¤¤PERCENTAGE¤¤¤"));
      input.Replace(_T("/_"), _T("¤¤¤ESCAPED¤¤¤UNDERSCORE¤¤¤"));
      input.Replace(_T("_"), _T("?"));
      input.Replace(_T("%"), _T("*"));
      input.Replace(_T("¤¤¤ESCAPED¤¤¤PERCENTAGE¤¤¤"), _T("%"));
      input.Replace(_T("¤¤¤ESCAPED¤¤¤UNDERSCORE¤¤¤"), _T("_"));

      return input;
   }

   namespace
   {
      void AssertGenerated(const SQLCommand &command, const String &expected)
      {
         SQLStatement statement;
         String actual = statement.GenerateFromCommand(command);

         if (actual != expected)
         {
            AnsiString message = "SQLStatement::GenerateFromCommand: expected [" + AnsiString(expected) +
               "], got [" + AnsiString(actual) + "].";
            throw std::logic_error(message.c_str());
         }
      }

      void AssertPositional(const SQLCommand &command, const String &expectedQuery, const std::vector<AnsiString> &expectedNames)
      {
         String actualQuery;
         std::vector<const SQLParameter *> actualParameters;

         if (!SQLStatement::ToPositionalParameters(command, actualQuery, actualParameters))
            throw std::logic_error(("SQLStatement::ToPositionalParameters refused [" + AnsiString(command.GetQueryString()) + "].").c_str());

         if (actualQuery != expectedQuery)
            throw std::logic_error(("SQLStatement::ToPositionalParameters: expected [" + AnsiString(expectedQuery) +
               "], got [" + AnsiString(actualQuery) + "].").c_str());

         bool sameNames = actualParameters.size() == expectedNames.size();
         for (size_t i = 0; sameNames && i < expectedNames.size(); i++)
            sameNames = actualParameters[i]->GetName() == String(expectedNames[i]);

         if (!sameNames)
            throw std::logic_error(("SQLStatement::ToPositionalParameters: wrong parameter order for [" + AnsiString(expectedQuery) + "].").c_str());
      }

      void AssertPositionalRefused(const SQLCommand &command)
      {
         String query;
         std::vector<const SQLParameter *> parameters;

         if (SQLStatement::ToPositionalParameters(command, query, parameters))
            throw std::logic_error(("SQLStatement::ToPositionalParameters accepted a parameter inside a literal: [" +
               AnsiString(command.GetQueryString()) + "].").c_str());
      }
   }

   void
   SQLStatementTester::Test()
   {
      // The values used here contain no backslash, since Escape doubles backslashes only
      // for some of the database types.

      // Strings are quoted and escaped, numbers written as they are.
      {
         SQLCommand command(_T("select * from t where a = @A and b = @B and c = @C"));
         command.AddParameter("@A", String(_T("O'Brien")));
         command.AddParameter("@B", 5);
         command.AddParameter("@C", (__int64) 12345678901);
         AssertGenerated(command, _T("select * from t where a = 'O''Brien' and b = 5 and c = 12345678901"));
      }

      // A value containing the name of a parameter substituted after it stays a value.
      // Before, the recipient was written into the sender's literal, and its own quotes
      // ended that literal, leaving the rest of the recipient as SQL.
      {
         SQLCommand command(_T("select * from hm_greylisting_triplets where glsenderaddress = @SENDERADDRESS and glrecipientaddress = @RECIPIENTADDRESS"));
         command.AddParameter("@SENDERADDRESS", String(_T("x@RECIPIENTADDRESS")));
         command.AddParameter("@RECIPIENTADDRESS", String(_T("OR/**/1=1#@example.test")));
         AssertGenerated(command, _T("select * from hm_greylisting_triplets where glsenderaddress = 'x@RECIPIENTADDRESS' and glrecipientaddress = 'OR/**/1=1#@example.test'"));
      }

      // The same with the parameters added the other way around, so that the value naming
      // the other parameter is written first, and a value containing its own name.
      {
         SQLCommand command(_T("select @A, @B"));
         command.AddParameter("@B", String(_T("@A' or '1'='1")));
         command.AddParameter("@A", String(_T("@A")));
         AssertGenerated(command, _T("select '@A', '@A'' or ''1''=''1'"));
      }

      // Names are matched as whole tokens, whatever order the parameters were added in.
      {
         SQLCommand command(_T("values (@ID, @IDX, @ID)"));
         command.AddParameter("@ID", 1);
         command.AddParameter("@IDX", 2);
         AssertGenerated(command, _T("values (1, 2, 1)"));
      }

      // Anything that is not a parameter name is copied unchanged.
      {
         SQLCommand command(_T("select @@IDENTITY, @UNKNOWN, 'a@b', @"));
         command.AddParameter("@KNOWN", 1);
         AssertGenerated(command, _T("select @@IDENTITY, @UNKNOWN, 'a@b', @"));
      }

      // A parameter inside a literal of the query, as the PostgreSQL interval of
      // PersistentFetchAccount once had. Written as text, it still works.
      {
         SQLCommand command(_T("update hm_fetchaccounts set fanexttry = current_timestamp + INTERVAL '@MINUTES minutes' where faid = @FAID"));
         command.AddParameter("@MINUTES", 15);
         command.AddParameter("@FAID", (__int64) 7);
         AssertGenerated(command, _T("update hm_fetchaccounts set fanexttry = current_timestamp + INTERVAL '15 minutes' where faid = 7"));
      }

      // An empty string is an empty literal, not a missing value.
      {
         SQLCommand command(_T("select * from t where a = @A"));
         command.AddParameter("@A", String(_T("")));
         AssertGenerated(command, _T("select * from t where a = ''"));
      }

      // Positional parameters: numbered in the order the names first appear, a name used
      // twice gets one position, and nothing else in the query changes.
      {
         SQLCommand command(_T("select @@IDENTITY, 'a@b' from t where a = @A and b = @B and c = @A and d = @UNKNOWN"));
         command.AddParameter("@B", 2);
         command.AddParameter("@A", String(_T("x@B")));
         command.AddParameter("@UNUSED", 3);
         AssertPositional(command, _T("select @@IDENTITY, 'a@b' from t where a = $1 and b = $2 and c = $1 and d = @UNKNOWN"), { "@A", "@B" });
      }

      // An escaped quote does not end the literal it is in, and a literal that has ended
      // leaves the parameters after it alone.
      {
         SQLCommand command(_T("select * from t where a = 'it''s' and b = @A"));
         command.AddParameter("@A", 1);
         AssertPositional(command, _T("select * from t where a = 'it''s' and b = $1"), { "@A" });
      }

      // A parameter inside a literal cannot be bound, and the query is refused.
      {
         SQLCommand command(_T("update t set d = current_timestamp + INTERVAL '@MINUTES minutes'"));
         command.AddParameter("@MINUTES", 15);
         AssertPositionalRefused(command);

         SQLCommand escaped(_T("select * from t where a = 'it''s @A'"));
         escaped.AddParameter("@A", 1);
         AssertPositionalRefused(escaped);
      }
   }
}
