// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#include <Ice/BasicStream.h> // Not included in Ice/Ice.h
#include <Freeze/DBException.h>
#include <Freeze/DBI.h>
#include <Freeze/EvictorI.h>
#include <Freeze/Initialize.h>
#include <sys/stat.h>

using namespace std;
using namespace Ice;
using namespace Freeze;

#ifdef _WIN32
#   define FREEZE_DB_MODE 0
#else
#   define FREEZE_DB_MODE (S_IRUSR | S_IWUSR)
#endif

void
Freeze::checkBerkeleyDBReturn(int ret, const string& prefix, const string& op)
{
    if (ret == 0)
    {
	return; // Everything ok
    }
    
    ostringstream s;
    s << prefix << op << ": " << db_strerror(ret);

    switch (ret)
    {
	case DB_LOCK_DEADLOCK:
	{
	    DBDeadlockException ex(__FILE__, __LINE__);;
	    ex.message = s.str();
	    throw ex;
	}

        case ENOENT: // The case that db->open was called with a non-existent database
	case DB_NOTFOUND:
	{
	    DBNotFoundException ex(__FILE__, __LINE__);
	    ex.message = s.str();
	    throw ex;
	}
	
	default:
	{
	    DBException ex(__FILE__, __LINE__);
	    ex.message = s.str();
	    throw ex;
	}
    }
}

Freeze::DBEnvironmentI::DBEnvironmentI(const CommunicatorPtr& communicator, const string& name) :
    _communicator(communicator),
    _trace(0),
    _dbEnv(0),
    _name(name)
{
    _errorPrefix = "Freeze::DBEnvironment(\"" + _name + "\"): ";
    _trace = atoi(_communicator->getProperties()->getProperty("Freeze.Trace.DB").c_str());

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "opening database environment \"" << _name << "\"";
    }

    checkBerkeleyDBReturn(db_env_create(&_dbEnv, 0), _errorPrefix, "db_env_create");

    checkBerkeleyDBReturn(_dbEnv->open(_dbEnv, _name.c_str(),
				       DB_CREATE |
				       DB_INIT_LOCK |
				       DB_INIT_LOG |
				       DB_INIT_MPOOL |
				       DB_INIT_TXN |
				       DB_RECOVER,
				       FREEZE_DB_MODE), _errorPrefix, "DB_ENV->open");
}

Freeze::DBEnvironmentI::~DBEnvironmentI()
{
    if (_dbEnv)
    {
	Warning out(_communicator->getLogger());
	out << _errorPrefix << "\"" << _name << "\" has not been closed";
    }
}

string
Freeze::DBEnvironmentI::getName()
{
    // No mutex lock necessary, _name is immutable
    return _name;
}

CommunicatorPtr
Freeze::DBEnvironmentI::getCommunicator()
{
    // No mutex lock necessary, _communicator is immutable
    return _communicator;
}

DBPtr
Freeze::DBEnvironmentI::openDB(const string& name, bool create)
{
    IceUtil::RecMutex::Lock sync(*this);

    if (!_dbEnv)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    map<string, DBPtr>::iterator p = _dbMap.find(name);
    if (p != _dbMap.end())
    {
	return p->second;
    }

    ::DB* db;
    checkBerkeleyDBReturn(db_create(&db, _dbEnv, 0), _errorPrefix, "db_create");
    
    try
    {
	return new DBI(_communicator, this, db, name, create);
    }
    catch(...)
    {
	//
	// Cleanup after a failure to open the database. Ignore any
	// errors.
	//
	p = _dbMap.find(name);
	if (p != _dbMap.end())
	{
	    _dbMap.erase(p);
	}
	db->close(db, 0);
	throw;
    }
}

DBTransactionPtr
Freeze::DBEnvironmentI::startTransaction()
{
    IceUtil::RecMutex::Lock sync(*this);

    return new DBTransactionI(_communicator, _dbEnv, _name);
}

void
Freeze::DBEnvironmentI::close()
{
    IceUtil::RecMutex::Lock sync(*this);

    if (!_dbEnv)
    {
	return;
    }

    while(!_dbMap.empty())
    {
	DBPtr db = _dbMap.begin()->second;
	db->close();
    }

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "closing database environment \"" << _name << "\"";
    }

    checkBerkeleyDBReturn(_dbEnv->close(_dbEnv, 0), _errorPrefix, "DB_ENV->close");

    _dbEnv = 0;
}

void
Freeze::DBEnvironmentI::add(const string& name, const DBPtr& db)
{
    IceUtil::RecMutex::Lock sync(*this);

    _dbMap[name] = db;
}

void
Freeze::DBEnvironmentI::remove(const string& name)
{
    IceUtil::RecMutex::Lock sync(*this);

    _dbMap.erase(name);
}

void
Freeze::DBEnvironmentI::eraseDB(const string& name)
{
    IceUtil::RecMutex::Lock sync(*this);

    //
    // The database should not be open.
    //
    assert(_dbMap.find(name) == _dbMap.end());

    ::DB* db;

    checkBerkeleyDBReturn(db_create(&db, _dbEnv, 0), _errorPrefix, "db_create");

    //
    // Any failure in remove will cause the database to be closed.
    //
    checkBerkeleyDBReturn(db->remove(db, name.c_str(), 0, 0), _errorPrefix, "DB->remove");
}

DBEnvironmentPtr
Freeze::initialize(const CommunicatorPtr& communicator, const string& name)
{
    return new DBEnvironmentI(communicator, name);
}

Freeze::DBTransactionI::DBTransactionI(const CommunicatorPtr& communicator, ::DB_ENV* dbEnv, const string& name) :
    _communicator(communicator),
    _trace(0),
    _tid(0),
    _name(name)
{
    _errorPrefix = "Freeze::DBTransaction(\"" + _name + "\"): ";
    _trace = atoi(_communicator->getProperties()->getProperty("Freeze.Trace.DB").c_str());

    if (_trace >= 2)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "starting transaction for environment \"" << _name << "\"";
    }
    
    checkBerkeleyDBReturn(txn_begin(dbEnv, 0, &_tid, 0), _errorPrefix, "txn_begin");
}

Freeze::DBTransactionI::~DBTransactionI()
{
    if (_tid)
    {
	Warning out(_communicator->getLogger());
	out << _errorPrefix << "transaction has not been committed or aborted";
    }
}

void
Freeze::DBTransactionI::commit()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_tid)
    {
	ostringstream s;
	s << _errorPrefix << "transaction has already been committed or aborted";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    if (_trace >= 2)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "committing transaction for environment \"" << _name << "\"";
    }
    
    checkBerkeleyDBReturn(txn_commit(_tid, 0), _errorPrefix, "txn_commit");

    _tid = 0;
}

void
Freeze::DBTransactionI::abort()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_tid)
    {
	ostringstream s;
	s << _errorPrefix << "transaction has already been committed or aborted";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    if (_trace >= 2)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "aborting transaction for environment \"" << _name << "\" due to deadlock";
    }
    
    checkBerkeleyDBReturn(txn_abort(_tid), _errorPrefix, "txn_abort");

    _tid = 0;
}

DBCursorI::DBCursorI(const ::Ice::CommunicatorPtr& communicator, const std::string& name, DBC* cursor) :
    _communicator(communicator),
    _trace(0),
    _name(name),
    _cursor(cursor)
{
    _errorPrefix = "Freeze::DBCursor(\"" + _name += "\"): ";
    _trace = atoi(_communicator->getProperties()->getProperty("Freeze.Trace.DB").c_str());

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "creating cursor for \"" << _name << "\"";
    }
}

DBCursorI::~DBCursorI()
{
    if (_cursor != 0)
    {
	Warning out(_communicator->getLogger());
	out << _errorPrefix << "\"" << _name << "\" has not been closed";
    }
}

Ice::CommunicatorPtr
DBCursorI::getCommunicator()
{
    // immutable
    return _communicator;
}

void
DBCursorI::curr(Key& key, Value& value)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    memset(&dbData, 0, sizeof(dbData));

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "reading current value from database \"" << _name << "\"";
    }

    checkBerkeleyDBReturn(_cursor->c_get(_cursor, &dbKey, &dbData, DB_CURRENT), _errorPrefix, "DBcursor->c_get");

    //
    // Copy the data from the read key & data
    //
    key = Key(static_cast<Byte*>(dbKey.data), static_cast<Byte*>(dbKey.data) + dbKey.size);
    value = Value(static_cast<Byte*>(dbData.data), static_cast<Byte*>(dbData.data) + dbData.size);
}

void
DBCursorI::set(const Value& value)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    memset(&dbData, 0, sizeof(dbData));
    dbData.data = const_cast<void*>(static_cast<const void*>(value.begin()));
    dbData.size = value.size();

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "setting current value in database \"" << _name << "\"";
    }

    //
    // Note that the dbKey element is ignored.
    //
    checkBerkeleyDBReturn(_cursor->c_put(_cursor, &dbKey, &dbData, DB_CURRENT), _errorPrefix, "DBcursor->c_set");
}

bool
DBCursorI::next()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    //
    // This does a 0 byte partial read of the data for efficiency
    // reasons.
    //
    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    dbKey.flags = DB_DBT_PARTIAL;
    memset(&dbData, 0, sizeof(dbData));
    dbData.flags = DB_DBT_PARTIAL;

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "moving to next value in database \"" << _name << "\"";
    }

    try
    {
	checkBerkeleyDBReturn(_cursor->c_get(_cursor, &dbKey, &dbData, DB_NEXT), _errorPrefix, "DBcursor->c_get");
    }
    catch(const DBNotFoundException&)
    {
	return false;
    }
    return true;
}

bool
DBCursorI::prev()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    //
    // This does a 0 byte partial read of the data for efficiency
    // reasons.
    //
    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    dbKey.flags = DB_DBT_PARTIAL;
    memset(&dbData, 0, sizeof(dbData));
    dbData.flags = DB_DBT_PARTIAL;

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "moving to previous value in database \"" << _name << "\"";
    }

    try
    {
	checkBerkeleyDBReturn(_cursor->c_get(_cursor, &dbKey, &dbData, DB_PREV), _errorPrefix, "DBcursor->c_get");
    }
    catch(const DBNotFoundException&)
    {
	return false;
    }
    return true;
}

void
DBCursorI::del()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "removing the current element in database \"" << _name << "\"";
    }

    checkBerkeleyDBReturn(_cursor->c_del(_cursor, 0), _errorPrefix, "DBcursor->c_del");
}

DBCursorPtr
DBCursorI::clone()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBC* cursor;
    _cursor->c_dup(_cursor, &cursor, DB_POSITION);
    return new DBCursorI(_communicator, _name, cursor);
}

void
DBCursorI::close()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_cursor)
    {
	return;
    }

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "closing cursor \"" << _name << "\"";
    }
  
    _cursor->c_close(_cursor);
    _cursor = 0;
}

Freeze::DBI::DBI(const CommunicatorPtr& communicator, const DBEnvironmentIPtr& dbEnvObj, ::DB* db,
		 const string& name, bool create) :
    _communicator(communicator),
    _trace(0),
    _dbEnvObj(dbEnvObj),
    _db(db),
    _name(name)
{
    _errorPrefix = "Freeze::DB(\"" + _name + "\"): ";
    _trace = atoi(_communicator->getProperties()->getProperty("Freeze.Trace.DB").c_str());

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "opening database \"" << _name << "\" in environment \"" << _dbEnvObj->getName() << "\"";
    }
    
    u_int32_t flags = (create) ? DB_CREATE : 0;
    checkBerkeleyDBReturn(_db->open(_db, _name.c_str(), 0, DB_BTREE, flags, FREEZE_DB_MODE), _errorPrefix,
			  "DB->open");

    _dbEnvObj->add(_name, this);
}

Freeze::DBI::~DBI()
{
    if (_db)
    {
	Warning out(_communicator->getLogger());
	out << _errorPrefix << "\"" << _name << "\" has not been closed";
    }
}

string
Freeze::DBI::getName()
{
    // No mutex lock necessary, _name is immutable
    return _name;
}

CommunicatorPtr
Freeze::DBI::getCommunicator()
{
    // No mutex lock necessary, _communicator is immutable
    return _communicator;
}

Long
Freeze::DBI::getNumberOfRecords()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    //
    // TODO: DB_FAST_STAT doesn't seem to do what the documentation says...
    //
    DB_BTREE_STAT* s;
    checkBerkeleyDBReturn(_db->stat(_db, &s, 0), _errorPrefix, "DB->stat");
    Long num = s->bt_ndata;
    free(s);
    return num;
}

DBCursorPtr
Freeze::DBI::getCursor()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBC* cursor;

    checkBerkeleyDBReturn(_db->cursor(_db, 0, &cursor, 0), _errorPrefix, "DB->cursor");

    //
    // Note that the read of the data is partial (that is the data
    // will not actually be read into memory since it isn't needed
    // yet).
    //
    DBT dbData, dbKey;
    memset(&dbData, 0, sizeof(dbData));
    dbData.flags = DB_DBT_PARTIAL;
    memset(&dbKey, 0, sizeof(dbKey));
    dbKey.flags = DB_DBT_PARTIAL;

    try
    {
	checkBerkeleyDBReturn(cursor->c_get(cursor, &dbKey, &dbData, DB_FIRST), _errorPrefix, "DBcursor->c_get");
    }
    catch(const DBNotFoundException&)
    {
	//
	// Cleanup.
	//
	cursor->c_close(cursor);
	throw;
    }

    return new DBCursorI(_communicator, _name, cursor);
}

DBCursorPtr
Freeze::DBI::getCursorAtKey(const Key& key)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBC* cursor;

    checkBerkeleyDBReturn(_db->cursor(_db, 0, &cursor, 0), _errorPrefix, "DB->cursor");

    //
    // Move to the requested record
    //
    DBT dbKey;
    memset(&dbKey, 0, sizeof(dbKey));

    //
    // Note that the read of the data is partial (that is the data
    // will not actually be read into memory since it isn't needed
    // yet).
    //
    DBT dbData;
    memset(&dbData, 0, sizeof(dbData));
    dbData.flags = DB_DBT_PARTIAL;

    dbKey.data = const_cast<void*>(static_cast<const void*>(key.begin()));
    dbKey.size = key.size();
    try
    {
	checkBerkeleyDBReturn(cursor->c_get(cursor, &dbKey, &dbData, DB_SET), _errorPrefix, "DBcursor->c_get");
    }
    catch(const DBNotFoundException&)
    {
	//
	// Cleanup on failure.
	//
	cursor->c_close(cursor);
	throw;
    }

    return new DBCursorI(_communicator, _name, cursor);
}

void
Freeze::DBI::put(const Key& key, const Value& value)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    memset(&dbData, 0, sizeof(dbData));
    dbKey.data = const_cast<void*>(static_cast<const void*>(key.begin()));
    dbKey.size = key.size();
    dbData.data = const_cast<void*>(static_cast<const void*>(value.begin()));
    dbData.size = value.size();

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "writing value in database \"" << _name << "\"";
    }

    checkBerkeleyDBReturn(_db->put(_db, 0, &dbKey, &dbData, 0), _errorPrefix, "DB->put");
}

bool
Freeze::DBI::contains(const Key& key)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey;
    memset(&dbKey, 0, sizeof(dbKey));
    dbKey.data = const_cast<void*>(static_cast<const void*>(key.begin()));
    dbKey.size = key.size();

    DBT dbData;
    memset(&dbData, 0, sizeof(dbData));
    dbData.flags = DB_DBT_PARTIAL;

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "checking key in database \"" << _name << "\"";
    }

    int rc = _db->get(_db, 0, &dbKey, &dbData, 0);
    if (rc == DB_NOTFOUND)
    {
	return false;
    }

    checkBerkeleyDBReturn(rc, _errorPrefix, "DB->get");
    return true;
}

Value
Freeze::DBI::get(const Key& key)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey, dbData;
    memset(&dbKey, 0, sizeof(dbKey));
    memset(&dbData, 0, sizeof(dbData));
    dbKey.data = const_cast<void*>(static_cast<const void*>(key.begin()));
    dbKey.size = key.size();

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "reading value from database \"" << _name << "\"";
    }
    
    checkBerkeleyDBReturn(_db->get(_db, 0, &dbKey, &dbData, 0), _errorPrefix, "DB->get");

    return Value(static_cast<Byte*>(dbData.data), static_cast<Byte*>(dbData.data) + dbData.size);
}

void
Freeze::DBI::del(const Key& key)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    DBT dbKey;
    memset(&dbKey, 0, sizeof(dbKey));
    dbKey.data = const_cast<void*>(static_cast<const void*>(key.begin()));
    dbKey.size = key.size();

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "deleting value from database \"" << _name << "\"";
    }
    
    checkBerkeleyDBReturn(_db->del(_db, 0, &dbKey, 0), _errorPrefix, "DB->del");
}

void
Freeze::DBI::clear()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    u_int32_t count; // ignored
    checkBerkeleyDBReturn(_db->truncate(_db, 0, &count, 0), _errorPrefix, "DB->truncate");
}

void
Freeze::DBI::close()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	return;
    }

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "closing database \"" << _name << "\"";
    }
    
    checkBerkeleyDBReturn(_db->close(_db, 0), _errorPrefix, "DB->close");

    _dbEnvObj->remove(_name);
    _dbEnvObj = 0;
    _db = 0;
}

void
Freeze::DBI::remove()
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	return;
    }

    if (_trace >= 1)
    {
	Trace out(_communicator->getLogger(), "DB");
	out << "removing database \"" << _name << "\"";
    }

    //
    // Remove first needs to close the database object. It's not
    // possible to remove an open database.
    //
    checkBerkeleyDBReturn(_db->close(_db, 0), _errorPrefix, "DB->close");

    //
    // Take a copy of the DBEnvironment to make cleanup easier.
    //
    DBEnvironmentIPtr dbEnvCopy = _dbEnvObj;

    _dbEnvObj->remove(_name);
    _dbEnvObj = 0;
    _db = 0;

    //
    // Ask the DBEnvironment to erase the database.
    //
    dbEnvCopy->eraseDB(_name);
}

EvictorPtr
Freeze::DBI::createEvictor(EvictorPersistenceMode persistenceMode)
{
    IceUtil::Mutex::Lock sync(*this);

    if (!_db)
    {
	ostringstream s;
	s << _errorPrefix << "\"" << _name << "\" has been closed";
	DBException ex(__FILE__, __LINE__);
	ex.message = s.str();
	throw ex;
    }

    return new EvictorI(this, persistenceMode);
}

//
// Print for the various exception types.
//
void
Freeze::DBDeadlockException::ice_print(ostream& out) const
{
    Exception::ice_print(out);
    out << ":\nunknown local exception";
}

void
Freeze::DBNotFoundException::ice_print(ostream& out) const
{
    Exception::ice_print(out);
    out << ":\nunknown local exception";
}

void
Freeze::DBException::ice_print(ostream& out) const
{
    Exception::ice_print(out);
    out << ":\nunknown local exception";
}
