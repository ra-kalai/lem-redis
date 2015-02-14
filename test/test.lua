#!/usr/bin/env lem
--
-- This file is part of lem-redis
--
-- lem-redis is free software: you can redistribute it and/or modify it
-- under the terms of the GNU Lesser General Public License as
-- published by the Free Software Foundation, either version 3 of
-- the License, or (at your option) any later version.
--
-- lem-redis is distributed in the hope that it will be useful, but
-- WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU Lesser General Public License for more details.
--
-- You should have received a copy of the GNU Lesser General Public
-- License along with lem-redis.  If not, see <http://www.gnu.org/licenses/>.
--

local redis = require 'lem.redis'
local utils = require('lem.utils')
local spawn = utils.spawn
print('hiredis version:', redis.version)

err, conn = redis.connect("localhost")
if (err) then
  print(err, conn)
  return ;
end


local iterationCount = 50000


function benchFun(testname, fun, count)
  local now = utils.updatenow()
  local is
  local err = false
  for i=1,count do
    if not fun() then
      err = true
      is = i
      break
    end
  end
  local rtime = utils.updatenow() - now

  if not err then
    print(string.format("%s> %d iteration in %3.8fs | 1x avg: %3.8fs | %f Hz",
      testname,
      count,
      rtime,
      rtime/count,
      1/(rtime/count)
    ))
  else
    print("!!!!!", testname, 'FAIL')
    print(string.format("%s> %d iteration in %3.8fs | 1x avg: %3.8fs | %f Hz",
      testname,
      is,
      rtime,
      rtime/is,
      1/(rtime/is)
    ))
  end
end


function pingtest(conn)
  benchFun("PING TEST",
      function ()
      ok, data = conn:command({'ping'})
      return ok == nil
      end, iterationCount)
end

function hsetgettest(conn)
  local count = 0
  
  benchFun("HSET/HGET TEST",
    function ()
      count = count + 1

      ok, data = conn:command({'hset', 'key', 'subkey'..count, 'payload'..count})
  
      if ok ~= nil then
        return false
      end
  
      ok, data = conn:command({'hget', 'key', 'subkey'..count})
  
      return data == 'payload' .. count
    end, iterationCount)
end

function hgettest(conn)
  conn:command({'hset', 'key', 'subkey', 'payload'})
  
  benchFun("HGET TEST",
    function ()
      ok, data = conn:command({'hget', 'key', 'subkey'})
      return ok == nil
    end, iterationCount)
end

function hsettest(conn)
  benchFun("HSET TEST",
    function ()
      ok, data = conn:command({'hset', 'key', 'subkey', 'plop-payload'})
      return ok == nil
    end, iterationCount)
end

local chain = {
  pingtest,
  hgettest,
  hsettest,
  hsetgettest,
}

local before = utils.updatenow()

print([==[

starting a SEQUENTIAL test
]==])

for i, v in pairs(chain) do
  v(conn)
end

local rtime = utils.updatenow() - before
print("\n" .. 'SEQUENTIAL TEST> took ' .. rtime.. ' to complete')


print([==[

starting a // test with 1 client
]==])

local finish = {}

before = utils.updatenow()

function taskisfinishing()
  if (#finish == #chain) then
    local rtime = utils.updatenow() - before
    print("\n" .. '// TEST 1 client> took ' .. rtime.. ' to complete')
    conn:close()
    parallel_multi()
  end
end

for i, v in pairs(chain) do
  spawn(function () 
    v(conn)
    finish[#finish+1] = utils.thisthread()
    taskisfinishing()
  end)
end

function taskparafinishing(connA)
  if (#finish == #chain) then
    local rtime = utils.updatenow() - before
    print("\n" .. '// TEST multiclient> took ' .. rtime.. ' to complete')
    for i, v in pairs(connA) do
      v:close()
    end
    iterationCount = 1000
    test_pub_sub(10)
  end
end

function parallel_multi()
  local connA = {}
  print([==[

starting a // test with ]==] .. #chain .. [==[ clients
  
creating clients...]==])

  for i, v in pairs(chain) do
    local err, conn = redis.connect("localhost")
    if (err) then
      print(err, conn)
      return ;
    end
    connA[i] = conn
  end

  print("clients created, starting test\n")

  finish = {}
  before = utils.updatenow()
  for i, v in pairs(chain) do
    spawn(function () 
      v(connA[i])
      finish[#finish+1] = utils.thisthread()
      taskparafinishing(connA)
    end)
  end
end


function test_pub_sub(suscriberCount)
  local connA = {}

  local testIsFinish = suscriberCount + 1
  function endoftest()
    testIsFinish = testIsFinish - 1
    if (testIsFinish == 0) then
      local rtime = utils.updatenow() - before
      print("\n" .. 'PUB/SUB test> took ' .. rtime.. " to complete\n")
      print('TEST SEEM SUCCESSFULL')
    end
  end

  before = utils.updatenow()

  print([==[

starting a PUB/SUB TEST: 1 publisher, ]==].. suscriberCount.. [==[ suscribers
]==])


  local err, publisher = redis.connect("localhost")
  if (err) then
    print(err, publisher)
    return ;
  end

  for i=1,suscriberCount do
    err, connA[i] = redis.connect("localhost")
  end
  
  local subscribeDone = {}

  function startPublishing()
    subscribeDone[#subscribeDone + 1] = true
    if #subscribeDone == suscriberCount then
      spawn(function ()
        print('All subscriber were started, starting a publisher thread')
        for i=1, iterationCount do
          for i2=1, suscriberCount do
            ok, data = publisher:command({'publish', 'channel' .. i2, 'message'..i})
            if ok ~= nil then
              print("published thread encounter an error at iteration", i, i2)
              os.exit(1)
            end
          end
        end
        print('the publisher thread will end; '.. (iterationCount*suscriberCount) .. ' message sent')
        publisher:close()
        endoftest()
      end)
    end
  end

  for i=1,suscriberCount do
    spawn(function ()
      local clientIndex = i
      local client = connA[clientIndex]
      local numberOfChannelToSubscribe = i
      local expectedMsg = {}
      for i=1, numberOfChannelToSubscribe do
        --print('subscribe ',client, i, utils.thisthread())
        ok, data = client:command({'subscribe', 'channel' .. i})
        expectedMsg['channel' .. i] = iterationCount
        --for i, v in pairs(data) do
        --  print(i, v)
        --end
      end

      startPublishing()

      local finished = false
      local messageListReceived = 0
      local messageReceived = 0

      while not finished do
        ok, message = client:getMsg()
        for i, v in pairs(message) do
          expectedMsg[v[2]] = expectedMsg[v[2]] - 1
          messageReceived = messageReceived + 1 
        end

        messageListReceived = messageListReceived + 1

        local shouldContinue = false

        for i, v in pairs(expectedMsg) do
          if v ~= 0 then
            shouldContinue = true
          end
        end

        if shouldContinue then
          finished = false
        else
          finished = true
        end
      end

      if (messageReceived ~= clientIndex*iterationCount) then
        print(string.format("subscriberTask %d, did'nt receive enought msg: %d expected: %d",
          messageReceived,
          clientIndex*iterationCount))
        os.exit(1)
      end

      print(string.format("subscriber task %s finished MessageList: %5d, Message:%5d",
            clientIndex, messageListReceived, messageReceived))
      client:close()
      endoftest()
      --print('subscriber thread, finished messageListReceived: ', messageListReceived, 'message received', messageReceived)
    end)
  end

end
