/*
* This file is part of lem-redis.
* Copyright 2015 Ralph Augé
*
* lem-redis is free software: you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation, either version 3 of
* the License, or (at your option) any later version.
*
* lem-redis is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with lem-redis. If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <string.h>
#include <lem.h>

#define LEM_REDIS_CLIENT_MT "lem.redis.conn"
#define LEM_REDIS_MAX_ARGS 1024

#define LEM_ERROR_REDIS_CONN_ERROR                  -0x01
        /* description will be sent by redis */
#define LEM_ERROR_REDIS_CONN_DC                     -0x01
static const char
        LEM_ERROR_REDIS_CONN_DC_STR[]               = "Disconnect";

#define LEM_ERROR_NO_CONTEXT                        -0x11
static const char
        LEM_ERROR_NO_CONTEXT_STR[]                  = "No redis context";

#define LEM_ERROR_REDIS_CMD_CAN_NOT_BE_ADDED        -0x12
static const char
        LEM_ERROR_REDIS_CMD_CAN_NOT_BE_ADDED_STR[]  = "Command can't be added";

#define LEM_ERROR_CMD_ERROR                         -0x21
        /* description will be sent by redis */


#if LUA_VERSION_NUM == 501
  #if LUAI_MAXCSTACK < LEM_REDIS_MAX_ARGS
	  #error define a valid LEM_REDIS_MAX_ARGS by yourself
  #endif
#endif

#if LUA_VERSION_NUM == 502
  #if LUAI_MAXSTACK < LEM_REDIS_MAX_ARGS
	  #error define a valid LEM_REDIS_MAX_ARGS by yourself
  #endif
  #define lua_objlen(a,b) lua_rawlen(a,b)
#endif

#include "hiredis-boilerplate.c"

static lua_State *lem_global_lua_state;

static int 
lem_redis_gc(lua_State *T) {
  struct RedisLibevEvents *r = lua_touserdata(T, 1);

  if (r->context != NULL) {
     redisAsyncDisconnect(r->context);
  }

  return 0;
}

static int
lem_redis_close(lua_State *T) {
  RedisLibevEvents *d = lua_touserdata(T, 1);

  if (d->context) {
    redisAsyncDisconnect(d->context);
  }

  d->context = NULL;

  return 0;
}


static void
lem_redis_connect_callback(const redisAsyncContext *redisAsyncContext, int status) {
  RedisLibevEvents *e = (RedisLibevEvents*) redisAsyncContext->data;
  e->pass = 0;

  if (status != REDIS_OK) {
    lua_pop(e->S, 2);
    lua_pushinteger(e->S, LEM_ERROR_REDIS_CONN_ERROR);
    lua_pushstring(e->S, e->context->errstr);
    lem_queue(e->S, 2);
    return ;
  }

  lem_queue(e->S, 2);
}


static int
lem_redis_connect(lua_State *T) {
  const char *conninfo = luaL_checkstring(T, 1);
	redisAsyncContext *context;

  if (strncasecmp(conninfo, "unix:", 5) == 0) {
     const char *path = conninfo + 5;
     context = redisAsyncConnectUnix(path);
  } else {
    char host[255];
    int port = 6379;
    sscanf(conninfo, "%254[^:]:%d",host,&port);
    context = redisAsyncConnect(host, port);
  }

  lua_pop(T, 1);

  if (context->err) {
    lua_pushinteger(T, LEM_ERROR_REDIS_CONN_ERROR);
    lua_pushstring(T, context->errstr);
    redisAsyncFree(context);
    return 2;
  }

  lua_pushnil(T);
  RedisLibevEvents *e = lua_newuserdata(T, sizeof(RedisLibevEvents));

  if (redisLibevAttach(LEM_ context, e) != REDIS_OK ) {
    lua_pop(T, 2);
    lua_pushinteger(T, LEM_ERROR_REDIS_CONN_ERROR);
    lua_pushstring(T, context->errstr);

    redisAsyncFree(context);
    return 2;
  }

  luaL_getmetatable(T, LEM_REDIS_CLIENT_MT);
  lua_setmetatable(T, -2);

  e->S = T;
  redisAsyncSetConnectCallback(context, lem_redis_connect_callback);

  return lua_yield(T, 2);
}

static void
lem_push_redis_reply(lua_State *L, redisReply *redisReply) {
  switch(redisReply->type) {
    case REDIS_REPLY_ERROR:
      lua_pushlstring(L, redisReply->str, redisReply->len);
      break;
    case REDIS_REPLY_STATUS:
      lua_pushlstring(L, redisReply->str, redisReply->len);
      break;
    case REDIS_REPLY_INTEGER:
      lua_pushinteger(L, redisReply->integer);
      break;
    case REDIS_REPLY_NIL:
      lua_pushnil(L);
      break;
    case REDIS_REPLY_STRING:
      lua_pushlstring(L, redisReply->str, redisReply->len);
      break;
    case REDIS_REPLY_ARRAY:
      {
        int i;
        lua_createtable(L, redisReply->elements, 0);
        for (i = 0;i < redisReply->elements;i+=1) {
          lem_push_redis_reply(L, redisReply->element[i]);
          lua_rawseti(L, -2, i + 1); /* Store sub-reply */
        }
        break;
      }
    default:
      {
        char err[256];
        sprintf(err, "unknown redis reply type: %d", redisReply->type);
        lua_pushstring(L, err);
        return ;
      }
  }
}

static void
lem_redis_command_callback(redisAsyncContext *context, void *reply, void *privdata) {
  redisReply *redisReply = reply;
  lua_State *S = privdata;
  RedisLibevEvents *d = (RedisLibevEvents*) context->data;

  //printf("==0 %d %p %s\n",lua_gettop(S), S, lua_typename(S, lua_type(S, -1)));

  if (d->pass == 1) {
    // if this is not the first time that this function is called
    // it mean that we are receiving msg from a channel 
    if (context->c.flags & REDIS_SUBSCRIBED) {
      // did we loose the connection ?
      if (context->c.flags & REDIS_DISCONNECTING) {
        // a coroutine is actually waiting for a msg, 
        // awake her with an error
        if ((S = d->S)) {
          lua_pushinteger(S, LEM_ERROR_REDIS_CONN_DC);
          lua_pushstring(S, LEM_ERROR_REDIS_CONN_DC_STR);
          lem_queue(S, 2);
          d->S = NULL;
        }
        return ;
      }

      // looks like there is spurious reply sometimes..
      if (redisReply == NULL) {
        return ;
      }

      S = lem_global_lua_state;

      lua_pushlightuserdata(S, lem_redis_command_callback);
      lua_gettable(S, LUA_REGISTRYINDEX);

      lua_pushlightuserdata(S, d);
      lua_pushlightuserdata(S, d);
      lua_rawget(S, -3);

      if (lua_isnil(S, -1)) {
        lua_pop(S, 1);
        lua_newtable(S);
      }

      int table_len = lua_objlen(S, -1);

      lua_pushinteger(S, table_len+1);
      lem_push_redis_reply(S, redisReply);

      // stack is now like this:
      //   [registry] table
      //   lightudata
      //   table for conn
      //   k
      //   v
      lua_rawset(S, -3);
      lua_rawset(S, -3);

      // pop our [registry] table out
      lua_pop(S, 1);

      // actually someone is already waiting for a msg
      if ((S = d->S)) {
        lua_pushnil(S);
        lua_pushlightuserdata(S, lem_redis_command_callback);
        lua_gettable(S, LUA_REGISTRYINDEX);

        lua_pushlightuserdata(S, d);
        lua_rawget(S, -2);
        lua_insert(S, 2);

        // push an empty table in registry
        lua_pushlightuserdata(S, d);
        lua_newtable(S);
        lua_rawset(S, -3);
        lua_pop(S, 1);

        // printf("==2 %d %p %s\n",lua_gettop(S), S, lua_typename(S, lua_type(S, -1)));
        lem_queue(S, 2);
        d->S = NULL;
      }
      return ;
    }
  }

  d->pass = 1;
  d->S = NULL;

  if (redisReply == NULL) {
    // All callbacks are called with a NULL reply when the context encountered an error.
    lua_pushinteger(S, LEM_ERROR_REDIS_CONN_DC);
    lua_pushstring(S, LEM_ERROR_REDIS_CONN_DC_STR);
    lem_queue(S, 2);
  } else {
    if (redisReply->type != REDIS_REPLY_ERROR) {
      lua_pushnil(S);
    } else {
      lua_pushnumber(S, LEM_ERROR_CMD_ERROR);
    }
    lem_push_redis_reply(S, redisReply);
    lem_queue(S, 2);
  }
}

static int
lem_redis_get_message(lua_State *T) {
  struct RedisLibevEvents *d = lua_touserdata(T, 1);

  lua_pushlightuserdata(T, lem_redis_command_callback);
  lua_gettable(T, LUA_REGISTRYINDEX);

  lua_pushlightuserdata(T, d);
  lua_rawget(T, -2);

  // no result yet, so yield.
  if (lua_isnil(T, -1)) {

    lua_pop(T, 3);

    // no context we are dc return so return an error directly
    if (d->context == NULL) {
      lua_pushinteger(T, LEM_ERROR_NO_CONTEXT);
      lua_pushstring(T, LEM_ERROR_NO_CONTEXT_STR);
      return 2;
    }

    d->S = T;
    return lua_yield(T, 0);
  }

  int table_len = lua_objlen(T, -1);

  // no result yet, so yield.
  if (table_len == 0) {
    lua_pop(T, 3);
    d->S = T;
    return lua_yield(T, 0);
  }

  lua_insert(T, 1);

  lua_pushnil(T);
  lua_insert(T, 1);

  lua_pushlightuserdata(T, d);
  lua_newtable(T);

  lua_rawset(T, -3);
  lua_pop(T, 2);
  //printf("=>> %d %p %p %s\n", lua_gettop(T),lua_touserdata(T, -2),d, lua_typename(T, lua_type(T, -2)));

  return 2;
}

static int
lem_redis_command(lua_State *T) {
  struct RedisLibevEvents *d = lua_touserdata(T, 1);
  static const char *argv[LEM_REDIS_MAX_ARGS];
  static size_t argvlen[LEM_REDIS_MAX_ARGS];
  int nargs = 0, i;

  if (d->context == NULL) {
    lua_pop(T, lua_gettop(T));
    lua_pushinteger(T, LEM_ERROR_NO_CONTEXT);
    lua_pushstring(T, LEM_ERROR_NO_CONTEXT_STR);
    return 2;
  }

  for (i = 1; i <= lua_objlen(T, 2); i++) {
    lua_rawgeti(T, 2, i);
    argv[nargs] = lua_tolstring(T, -1, &argvlen[nargs]);
    lua_pop(T, 1);

    if (argv[nargs] == NULL) {
      return luaL_argerror(T, i, "string or number expected");
    }

    nargs += 1;
    if (nargs >= LEM_REDIS_MAX_ARGS) {
      return luaL_error(T, "too many arguments");
    }
  }


  lua_pop(T, lua_gettop(T));

  d->S = T;
  d->pass = 0;
  int commandStatus = redisAsyncCommandArgv(d->context,
                                            lem_redis_command_callback,
                                            T,
                                            nargs,
                                            argv,
                                            argvlen);

  if (commandStatus != REDIS_OK) {
    lua_pop(T, lua_gettop(T));
    lua_pushinteger(T, LEM_ERROR_REDIS_CMD_CAN_NOT_BE_ADDED);
    lua_pushstring(T, LEM_ERROR_REDIS_CMD_CAN_NOT_BE_ADDED_STR);
    return 2;
  }

  return lua_yield(T, 0);
}

static const luaL_Reg lem_redis_con_mt[] = {
  {"__gc",        lem_redis_gc },
  {"close",       lem_redis_close },
  {"command",     lem_redis_command },
  {"getMsg",      lem_redis_get_message },
  {NULL, NULL },
};

static const luaL_Reg lem_redis_export[] = {
  {"connect", lem_redis_connect},
  {NULL, NULL },
};

#define VERSION(a,b,c) OUT_STR(a) "." OUT_STR(b) "." OUT_STR(c)
#define OUT_STR(a) #a

static void
h_set_methods(lua_State *L,const luaL_Reg *func_list) {
  for(;*func_list->func!=NULL;func_list++) {
    lua_pushcfunction(L, func_list->func);
    lua_setfield(L, -2, func_list->name);
  }
}

int
luaopen_lem_redis(lua_State *L) {
  lem_global_lua_state = lem_get_global_lua_state();

  lua_pushlightuserdata(L, lem_redis_command_callback);
  lua_newtable(L);
  lua_settable(L, LUA_REGISTRYINDEX);


  luaL_newmetatable(L, LEM_REDIS_CLIENT_MT);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  h_set_methods(L, lem_redis_con_mt);

  lua_newtable(L);
  lua_pushstring(L, VERSION(HIREDIS_MAJOR, HIREDIS_MINOR, HIREDIS_PATCH));
  lua_setfield(L, -2, "version");
  h_set_methods(L, lem_redis_export);

  return 1;
}
