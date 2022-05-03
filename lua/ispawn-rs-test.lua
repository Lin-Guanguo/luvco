luvco.import("chan")
luvco.import("lualibs")

local sender = luvco.ispawn_s(
    --function ()
    [[
        luvco.import("lualibs")
        for i = 1, 5 do
            local ok, r = recv_parent:recv()
            print ("recv over", ok, r)
        end
    ]]
    --end
)

for i = 1, 5 do
    local ok = sender:send(i)
    print ("send over", ok)
end

local recver = luvco.ispawn_r(
    --function ()
    [[
        luvco.import("lualibs")
        for i = 6, 10 do
            local ok = send_parent:send(i)
            print ("send over", ok)
        end
    ]]
    --end
)

for i = 6, 10 do
    local ok, r = recver:recv()
    print ("recv over", ok, r)
end

recver, sender = luvco.ispawn_rs(
    --function ()
    [[
        luvco.import("lualibs")
        for i = 11, 15 do
            local ok, r = recv_parent:recv()
            print ("recv over", ok, r)
            ok = send_parent:send(i)
            print ("send over", ok)
        end
    ]]
    --end
)

for i = 16, 20 do
    local ok = sender:send(i)
    print ("send over", ok)
    local ok, r = recver:recv()
    print ("recv over", ok, r)
end

collectgarbage("collect")
print ("============== close test =================")
sender = luvco.ispawn_s("")
do
    local ok = sender:send(1);
    print ("send to close coro, over", ok)
end

recver = luvco.ispawn_r("")
do
    local ok, r = recver:recv();
    print ("recv from close coro, over", ok, r)
end