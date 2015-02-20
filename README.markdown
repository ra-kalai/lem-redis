lem-redis
============


About
-----

lem-redis is a [Redis][Redis] library using [Hiredis][Hiredis] for the [Lua Event Machine][lem] 
It allows you to query a redis data structure server without blocking other coroutines.

[lem]: https://github.com/esmil/lem
[Redis]: http://redis.io/
[Hiredis]: https://github.com/redis/hiredis

Installation
------------

Get the source and do

    make

    # run test / benchmark
    make test
   
    # install
    make install

Usage
-----

Import the module using something like

    local redis = require 'lem.redis'

This sets `redis` to a table with a single function.

* __redis.connect(conninfo)__

  Connect to the Redis data structure server given by parameters in the `conninfo` string.
  Returns nil and a new data store, connection object on success
  or otherwise a negative integer, followed by an error message.


The Data structure server, connection object has a metatable with the following methods:

* __conn:close()__

  Close the Redis connection.

* __conn:command({})__

  Execute a Redis command, and yield the current coroutine.

  Returns nil and a result on success
  or otherwise a negative interger, followed by an error message

* __conn:getMsg()__

  Get all the message from the channel you have subscribed to.
  If no message are available, yield the current coroutine, untill a
  message arrive.

  Returns nil and a table (list) of messages result on success
  or otherwise return a negative integer, followed by an error message

Performance
-------
  
    Intel(R) Core(TM) i5-3470S CPU @ 2.90GHz
    Linux 3.13.0-32-generic #57~precise1-Ubuntu SMP Tue Jul 15 03:51:20 UTC 2014 x86_64 x86_64 x86_64 GNU/Linux
    lua 5.1 / Redis 2.8.13 

    hiredis version:  0.12.1
    
    starting a SEQUENTIAL test
    
    PING TEST> 50000 iteration in 1.77578282s | 1x avg: 0.00003552s | 28156.596255 Hz
    HGET TEST> 50000 iteration in 1.79653430s | 1x avg: 0.00003593s | 27831.363979 Hz
    HSET TEST> 50000 iteration in 1.78283954s | 1x avg: 0.00003566s | 28045.148748 Hz
    HSET/HGET TEST> 50000 iteration in 3.85923505s | 1x avg: 0.00007718s | 12955.935405 Hz
    
    SEQUENTIAL TEST> took 9.2146050930023 to complete
    
    starting a // test with 1 client
    
    PING TEST> 50000 iteration in 2.69307613s | 1x avg: 0.00005386s | 18566.129406 Hz
    HSET TEST> 50000 iteration in 2.69308901s | 1x avg: 0.00005386s | 18566.040649 Hz
    HGET TEST> 50000 iteration in 2.69306803s | 1x avg: 0.00005386s | 18566.185291 Hz
    HSET/HGET TEST> 50000 iteration in 5.08080721s | 1x avg: 0.00010162s | 9840.955963 Hz
    
    // TEST 1 client> took 5.0809111595154 to complete
    
    starting a // test with 4 clients
      
    creating clients...
    clients created, starting test
    
    PING TEST> 50000 iteration in 2.63488674s | 1x avg: 0.00005270s | 18976.147707 Hz
    HGET TEST> 50000 iteration in 2.63484406s | 1x avg: 0.00005270s | 18976.455066 Hz
    HSET TEST> 50000 iteration in 2.63503408s | 1x avg: 0.00005270s | 18975.086621 Hz
    HSET/HGET TEST> 50000 iteration in 5.05674863s | 1x avg: 0.00010113s | 9887.776449 Hz
    
    // TEST multiclient> took 5.056853055954 to complete
    
    starting a PUB/SUB TEST: 1 publisher, 10 suscribers
    
    All subscriber were started, starting a publisher thread
    subscriber task 1 finished MessageList:  1000, Message: 1000
    subscriber task 2 finished MessageList:  2000, Message: 2000
    subscriber task 3 finished MessageList:  3000, Message: 3000
    subscriber task 4 finished MessageList:  4000, Message: 4000
    subscriber task 5 finished MessageList:  5000, Message: 5000
    subscriber task 6 finished MessageList:  6000, Message: 6000
    subscriber task 7 finished MessageList:  7000, Message: 7000
    subscriber task 8 finished MessageList:  8000, Message: 8000
    subscriber task 9 finished MessageList:  9000, Message: 9000
    subscriber task 10 finished MessageList: 10000, Message:10000
    the publisher thread will end; 10000 message sent
    
    PUB/SUB test> took 0.85416889190674 to complete
  

License
-------

lem-redis is free software. It is distributed both under the terms of the
[GNU General Public License][gpl] any revision, and the [GNU Lesser General Public License][lgpl] any revision.   
Except _lem/hiredis-boilerplate.c_ and hiredis/* which are under a
[Three clause BSD license][threeclosebsd]

[gpl]: http://www.fsf.org/licensing/licenses/gpl.html
[lgpl]: http://www.fsf.org/licensing/licenses/lgpl.html
[threeclosebsd]: https://raw.githubusercontent.com/redis/hiredis/master/COPYING


Contact
-------

Please send bug reports, patches and feature requests to me <ra@apathie.net>.
