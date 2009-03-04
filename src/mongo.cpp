// mongo.cpp
/**
 *  Copyright 2009 10gen, Inc.
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <vector>

#include <php.h>
#include <php_ini.h>
#include <mongo/client/dbclient.h>
#include <mongo/client/gridfs.h>

#include "mongo.h"
#include "mongo_id.h"
#include "mongo_date.h"
#include "mongo_regex.h"
#include "mongo_bindata.h"
#include "bson.h"
#include "gridfs.h"
#include "auth.h"

zend_class_entry *mongo_id_class;
zend_class_entry *mongo_date_class;
zend_class_entry *mongo_regex_class;
zend_class_entry *mongo_bindata_class;

/** Resources */
int le_connection;
int le_pconnection;
int le_db_cursor;
int le_gridfs;
int le_gridfile;

static function_entry mongo_functions[] = {
  PHP_FE( mongo_connect , NULL )
  PHP_FE( mongo_pconnect , NULL )
  PHP_FE( mongo_close , NULL )
  PHP_FE( mongo_remove , NULL )
  PHP_FE( mongo_query , NULL )
  PHP_FE( mongo_find_one , NULL )
  PHP_FE( mongo_insert , NULL )
  PHP_FE( mongo_batch_insert , NULL )
  PHP_FE( mongo_update , NULL )
  PHP_FE( mongo_has_next , NULL )
  PHP_FE( mongo_next , NULL )
  PHP_FE( mongo_auth_connect , NULL )
  PHP_FE( mongo_auth_get , NULL )
  PHP_FE( mongo_gridfs_init , NULL )
  PHP_FE( mongo_gridfs_list , NULL )
  PHP_FE( mongo_gridfs_store , NULL )
  PHP_FE( mongo_gridfs_find , NULL )
  PHP_FE( mongo_gridfile_filename , NULL )
  PHP_FE( mongo_gridfile_size , NULL )
  PHP_FE( mongo_gridfile_write , NULL )
  {NULL, NULL, NULL}
};

static function_entry mongo_id_functions[] = {
  PHP_NAMED_FE( __construct, PHP_FN( mongo_id___construct ), NULL )
  PHP_NAMED_FE( __toString, PHP_FN( mongo_id___toString ), NULL )
  { NULL, NULL, NULL }
};


static function_entry mongo_date_functions[] = {
  PHP_NAMED_FE( __construct, PHP_FN( mongo_date___construct ), NULL )
  PHP_NAMED_FE( __toString, PHP_FN( mongo_date___toString ), NULL )
  { NULL, NULL, NULL }
};


static function_entry mongo_regex_functions[] = {
  PHP_NAMED_FE( __construct, PHP_FN( mongo_regex___construct ), NULL )
  PHP_NAMED_FE( __toString, PHP_FN( mongo_regex___toString ), NULL )
  { NULL, NULL, NULL }
};

static function_entry mongo_bindata_functions[] = {
  PHP_NAMED_FE( __construct, PHP_FN( mongo_bindata___construct ), NULL )
  PHP_NAMED_FE( __toString, PHP_FN( mongo_bindata___toString ), NULL )
  { NULL, NULL, NULL }
};

zend_module_entry mongo_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
  STANDARD_MODULE_HEADER,
#endif
  PHP_MONGO_EXTNAME,
  mongo_functions,
  PHP_MINIT(mongo),
  NULL,
  NULL,
  NULL,
  NULL,
#if ZEND_MODULE_API_NO >= 20010901
  PHP_MONGO_VERSION,
#endif
    STANDARD_MODULE_PROPERTIES
};

#ifdef COMPILE_DL_MONGO
ZEND_GET_MODULE(mongo)
#endif


static void php_connection_dtor( zend_rsrc_list_entry *rsrc TSRMLS_DC ) {
  mongo::DBClientConnection *conn = (mongo::DBClientConnection*)rsrc->ptr;
  if( conn )
    delete conn;
}

static void php_gridfs_dtor( zend_rsrc_list_entry *rsrc TSRMLS_DC ) {
  mongo::GridFS *fs = (mongo::GridFS*)rsrc->ptr;
  if( fs )
    delete fs;
}

static void php_gridfile_dtor( zend_rsrc_list_entry *rsrc TSRMLS_DC ) {
  mongo::GridFile *file = (mongo::GridFile*)rsrc->ptr;
  if( file )
    delete file;
}


PHP_MINIT_FUNCTION(mongo) {

  le_connection = zend_register_list_destructors_ex(php_connection_dtor, NULL, PHP_CONNECTION_RES_NAME, module_number);
  le_pconnection = zend_register_list_destructors_ex(NULL, php_connection_dtor, PHP_CONNECTION_RES_NAME, module_number);
  le_db_cursor = zend_register_list_destructors_ex(NULL, NULL, PHP_DB_CURSOR_RES_NAME, module_number);
  le_gridfs = zend_register_list_destructors_ex(php_gridfs_dtor, NULL, PHP_GRIDFS_RES_NAME, module_number);
  le_gridfile = zend_register_list_destructors_ex(php_gridfile_dtor, NULL, PHP_GRIDFILE_RES_NAME, module_number);

  zend_class_entry id; 
  INIT_CLASS_ENTRY(id, "MongoId", mongo_id_functions); 
  mongo_id_class = zend_register_internal_class(&id TSRMLS_CC); 

  zend_class_entry date; 
  INIT_CLASS_ENTRY(date, "MongoDate", mongo_date_functions); 
  mongo_date_class = zend_register_internal_class(&date TSRMLS_CC); 

  zend_class_entry regex; 
  INIT_CLASS_ENTRY(regex, "MongoRegex", mongo_regex_functions); 
  mongo_regex_class = zend_register_internal_class(&regex TSRMLS_CC); 

  zend_class_entry bindata; 
  INIT_CLASS_ENTRY(bindata, "MongoBinData", mongo_bindata_functions); 
  mongo_bindata_class = zend_register_internal_class(&bindata TSRMLS_CC); 

  return SUCCESS;
}


/* {{{ mongo_connect
 */
PHP_FUNCTION(mongo_connect) {
  php_mongo_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 0);
}
/* }}} */


/* {{{ mongo_pconnect
 */
PHP_FUNCTION(mongo_pconnect) {
  php_mongo_do_connect(INTERNAL_FUNCTION_PARAM_PASSTHRU, 1);
}
/* }}} */


/* {{{ proto bool mongo_close(resource connection) 
   Closes the database connection */
PHP_FUNCTION(mongo_close) {
  zval *zconn;
 
  if (ZEND_NUM_ARGS() != 1 ) {
     zend_error( E_WARNING, "expected 1 parameter, got %d parameters", ZEND_NUM_ARGS() );
     RETURN_FALSE;
  }
  else if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &zconn) == FAILURE) {
     zend_error( E_WARNING, "incorrect parameter types, expected mongo_close( connection )" );
     RETURN_FALSE;
  }

  zend_list_delete(Z_LVAL_P(zconn));
  RETURN_TRUE;
}
/* }}} */


/* {{{ proto cursor mongo_query(resource connection, string ns, array query, int limit, int skip, array sort, array fields, array hint) 
   Query the database */
PHP_FUNCTION(mongo_query) {
  zval *zconn, *zquery, *zsort, *zfields, *zhint;
  char *collection;
  int limit, skip, collection_len;
  mongo::DBClientConnection *conn_ptr;

  if( ZEND_NUM_ARGS() != 8 ) {
      zend_error( E_WARNING, "expected 8 parameters, got %d parameters", ZEND_NUM_ARGS() );
      RETURN_FALSE;
  }
  else if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsallaaa", &zconn, &collection, &collection_len, &zquery, &skip, &limit, &zsort, &zfields, &zhint) == FAILURE ) {
      zend_error( E_WARNING, "incorrect parameter types, expected mongo_query( connection, string, array, int, int, array, array, array )" );
      RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  mongo::BSONObjBuilder *bquery = new mongo::BSONObjBuilder();
  php_array_to_bson( bquery, Z_ARRVAL_P( zquery ) );
  mongo::BSONObj query = bquery->done();
  mongo::Query *q = new mongo::Query( query );

  mongo::BSONObjBuilder *bfields = new mongo::BSONObjBuilder();
  int num_fields = php_array_to_bson( bfields, Z_ARRVAL_P( zfields ) );
  mongo::BSONObj fields = bfields->done();

  mongo::BSONObjBuilder *bhint = new mongo::BSONObjBuilder();
  int n = php_array_to_bson( bhint, Z_ARRVAL_P( zhint ) );
  if( n > 0 ) {
    mongo::BSONObj hint = bhint->done();
    q->hint( hint );
  }

  mongo::BSONObjBuilder *bsort = new mongo::BSONObjBuilder();
  n = php_array_to_bson( bsort, Z_ARRVAL_P( zsort ) );
  if( n > 0 ) {
    mongo::BSONObj sort = bsort->done();
    q->sort( sort );
  }

  std::auto_ptr<mongo::DBClientCursor> cursor;
  if( num_fields == 0 )
    cursor = conn_ptr->query( (const char*)collection, *q, limit, skip );
  else
    cursor = conn_ptr->query( (const char*)collection, *q, limit, skip, &fields );

  delete bquery;
  delete bfields;
  delete bhint;
  delete bsort;

  mongo::DBClientCursor *c = cursor.get();
  ZEND_REGISTER_RESOURCE( return_value, c, le_db_cursor );
}
/* }}} */


/* {{{ proto array mongo_find_one(resource connection, string ns, array query) 
   Query the database for one record */
PHP_FUNCTION(mongo_find_one) {
  zval *zconn, *zquery;
  char *collection;
  int collection_len;
  mongo::BSONObj query;
  mongo::DBClientConnection *conn_ptr;

  if( ZEND_NUM_ARGS() != 3 ) {
    zend_error( E_WARNING, "expected 3 parameters, got %d parameters", ZEND_NUM_ARGS() );
    RETURN_FALSE;
  }
  else if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsa", &zconn, &collection, &collection_len, &zquery) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_find_one( connection, string, array )" );
    RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  mongo::BSONObjBuilder *bquery = new mongo::BSONObjBuilder();
  php_array_to_bson( bquery, Z_ARRVAL_P( zquery ) );
  query = bquery->done();

  mongo::BSONObj obj = conn_ptr->findOne( (const char*)collection, query );
  zval *array = bson_to_php_array( obj );
  RETURN_ZVAL( array, 0, 1 );
}
/* }}} */


/* {{{ proto bool mongo_remove(resource connection, string ns, array query) 
   Remove records from the database */
PHP_FUNCTION(mongo_remove) {
  zval *zconn, *zarray;
  char *collection;
  int collection_len;
  zend_bool justOne = 0;
  mongo::DBClientConnection *conn_ptr;

  if( ZEND_NUM_ARGS() != 4 ) {
    zend_error( E_WARNING, "expected 4 parameters, got %d parameters", ZEND_NUM_ARGS() );
    RETURN_FALSE;
  }
  else if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsab", &zconn, &collection, &collection_len, &zarray, &justOne) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_remove( connection, string, array, bool )" );
    RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  mongo::BSONObjBuilder *rarray = new mongo::BSONObjBuilder(); 
  php_array_to_bson( rarray, Z_ARRVAL_P(zarray) );
  conn_ptr->remove( collection, rarray->done(), justOne );
  RETURN_TRUE;
}
/* }}} */


/* {{{ proto bool mongo_insert(resource connection, string ns, array obj) 
   Insert a record to the database */
PHP_FUNCTION(mongo_insert) {
  zval *zconn, *zarray;
  char *collection;
  int collection_len;
  mongo::DBClientConnection *conn_ptr;

  if (ZEND_NUM_ARGS() != 3 ) {
    zend_error( E_WARNING, "expected 3 parameters, got %d parameters", ZEND_NUM_ARGS() );
    RETURN_FALSE;
  }
  else if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsa", &zconn, &collection, &collection_len, &zarray) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_insert( connection, string, array )" );
    RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  mongo::BSONObjBuilder *obj_builder = new mongo::BSONObjBuilder();
  HashTable *php_array = Z_ARRVAL_P(zarray);
  if( !zend_hash_exists( php_array, "_id", 3 ) )
      prep_obj_for_db( obj_builder );
  php_array_to_bson( obj_builder, php_array );
  conn_ptr->insert( collection, obj_builder->done() );
  RETURN_TRUE;
}
/* }}} */

PHP_FUNCTION(mongo_batch_insert) {
  zval *zconn, *zarray;
  char *collection;
  int collection_len;
  mongo::DBClientConnection *conn_ptr;

  if (ZEND_NUM_ARGS() != 3 ) {
    zend_error( E_WARNING, "expected 3 parameters, got %d parameters", ZEND_NUM_ARGS() );
    RETURN_FALSE;
  }
  else if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rsa", &zconn, &collection, &collection_len, &zarray) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_insert( connection, string, array )" );
    RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  vector<mongo::BSONObj> inserter;
  HashTable *php_array = Z_ARRVAL_P(zarray);
  HashPosition pointer;
  zval **data;
  for(zend_hash_internal_pointer_reset_ex(php_array, &pointer); 
      zend_hash_get_current_data_ex(php_array, (void**) &data, &pointer) == SUCCESS; 
      zend_hash_move_forward_ex(php_array, &pointer)) {
    mongo::BSONObjBuilder *obj_builder = new mongo::BSONObjBuilder();
    HashTable *insert_elem = Z_ARRVAL_PP(data);

    php_array_to_bson(obj_builder, insert_elem);
    if (!zend_hash_exists(insert_elem, "_id", 3)) {
      prep_obj_for_db(obj_builder);
    }
    inserter.push_back(obj_builder->done());
  }

  conn_ptr->insert(collection, inserter);
  RETURN_TRUE;
}


/* {{{ proto bool mongo_update(resource connection, string ns, array query, array replacement, bool upsert) 
   Update a record in the database */
PHP_FUNCTION(mongo_update) {
  zval *zconn, *zquery, *zobj;
  char *collection;
  int collection_len;
  zend_bool zupsert = 0;
  mongo::DBClientConnection *conn_ptr;
  int num_args = ZEND_NUM_ARGS();
  if ( num_args != 5 ) {
    zend_error( E_WARNING, "expected 5 parameters, got %d parameters", num_args );
    RETURN_FALSE;
  }
  else if(zend_parse_parameters(num_args TSRMLS_CC, "rsaab", &zconn, &collection, &collection_len, &zquery, &zobj, &zupsert) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_update( connection, string, array, array, bool )");
    RETURN_FALSE;
  }

  ZEND_FETCH_RESOURCE2(conn_ptr, mongo::DBClientConnection*, &zconn, -1, PHP_CONNECTION_RES_NAME, le_connection, le_pconnection); 

  mongo::BSONObjBuilder *bquery =  new mongo::BSONObjBuilder();
  php_array_to_bson( bquery, Z_ARRVAL_P( zquery ) );
  mongo::BSONObjBuilder *bfields = new mongo::BSONObjBuilder();
  php_array_to_bson( bfields, Z_ARRVAL_P( zobj ) );
  conn_ptr->update( collection, bquery->done(), bfields->done(), (int)zupsert );
  RETURN_TRUE;
}
/* }}} */


/* {{{ proto bool mongo_has_next(resource cursor) 
   Check if a cursor has another record. */
PHP_FUNCTION( mongo_has_next ) {
  zval *zcursor;

  int argc = ZEND_NUM_ARGS();
  if (argc != 1 ) {
    zend_error( E_WARNING, "expected 1 parameters, got %d parameters", argc );
    RETURN_FALSE;
  }
  else if( zend_parse_parameters(argc TSRMLS_CC, "r", &zcursor) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter types, expected mongo_has_next( cursor ), got %d parameters", argc );
    RETURN_FALSE;
  }

  mongo::DBClientCursor *c = (mongo::DBClientCursor*)zend_fetch_resource(&zcursor TSRMLS_CC, -1, PHP_DB_CURSOR_RES_NAME, NULL, 1, le_db_cursor);

  bool more = c->more();
  RETURN_BOOL(more);
}
/* }}} */


/* {{{ proto array mongo_next(resource cursor) 
   Get the next record from a cursor */
PHP_FUNCTION( mongo_next ) {
  zval *zcursor;

  int argc = ZEND_NUM_ARGS();
  if (argc != 1 ) {
    zend_error( E_WARNING, "expected 1 parameter, got %d parameters", argc );
    RETURN_FALSE;
  }
  else if(zend_parse_parameters(argc TSRMLS_CC, "r", &zcursor) == FAILURE) {
    zend_error( E_WARNING, "incorrect parameter type, expected mongo_next( cursor )" );
    RETURN_FALSE;
  }

  mongo::DBClientCursor *c = (mongo::DBClientCursor*)zend_fetch_resource(&zcursor TSRMLS_CC, -1, PHP_DB_CURSOR_RES_NAME, NULL, 1, le_db_cursor);

  mongo::BSONObj bson = c->next();
  zval *array = bson_to_php_array( bson );
  RETURN_ZVAL( array, 0, 1 );
}
/* }}} */


/* {{{ php_mongo_do_connect
 */
static void php_mongo_do_connect(INTERNAL_FUNCTION_PARAMETERS, int persistent) {
  mongo::DBClientConnection *conn;
  char *server, *uname, *pass, *key;
  zend_bool auto_reconnect, lazy;
  int server_len, uname_len, pass_len, key_len;
  list_entry le, *le_ptr;
  string error;
  void *foo;
  
  int argc = ZEND_NUM_ARGS();
  if (persistent) {
    if (argc != 5 ||
        zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sssbb", &server, &server_len, &uname, &uname_len, &pass, &pass_len, &auto_reconnect, &lazy) == FAILURE) {
      zend_error( E_WARNING, "parameter parse failure, expected: mongo_auth_connect( string, string, string, bool, bool )" );
      RETURN_FALSE;
    }

    key_len = spprintf(&key, 0, "%s_%s_%s", server, uname, pass);
    if (zend_hash_find(&EG(persistent_list), key, key_len + 1, &foo) == SUCCESS) {
      php_printf("found a connection for %s", key);
      le_ptr = (list_entry*)foo;
      conn = (mongo::DBClientConnection*)le_ptr->ptr;
    }
    efree(key);

    /* if a connection was found, return it */
    if (conn) { 
      ZEND_REGISTER_RESOURCE(return_value, conn, le_pconnection);
      return;
    }
    /* if lazy and no connection was found, return */
    else if(lazy) {
      RETURN_NULL();
    }
  }
  /* non-persistent */
  else {
    if( ZEND_NUM_ARGS() != 2 ) {
      zend_error( E_WARNING, "expected 2 parameters, got %d parameters", ZEND_NUM_ARGS() );
      RETURN_FALSE;
    }
    else if( zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "sb", &server, &server_len, &auto_reconnect) == FAILURE ) {
      zend_error( E_WARNING, "incorrect parameter types, expected: mongo_connect( string, bool )" );
      RETURN_FALSE;
    }
  }

  if ( server_len == 0 ) {
    zend_error( E_WARNING, "invalid host" );
    RETURN_FALSE;
  }

  conn = new mongo::DBClientConnection( (bool)auto_reconnect );
  if ( ! conn->connect( server, error ) ){
    zend_error( E_WARNING, "%s", error.c_str() );
    RETURN_FALSE;
  }

  // store a reference in the persistence list
  if (persistent) {
    key_len = spprintf(&key, 0, "%s_%s_%s", server, uname, pass);
    le.ptr = conn;
    le.type = le_connection;
    zend_hash_add(&EG(persistent_list), key, key_len + 1, &le, sizeof(list_entry), NULL);
    efree(key);

    ZEND_REGISTER_RESOURCE(return_value, conn, le_pconnection);
  }
  // otherwise, just return the connection
  else {
    ZEND_REGISTER_RESOURCE(return_value, conn, le_connection);    
  }
}
/* }}} */
