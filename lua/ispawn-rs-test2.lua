luvco.import("chan")
luvco.import("lualibs")

local sender = luvco.ispawn_s("")
do
    local ok = sender:send(1);
    print ("send to close coro, over", ok)
end

local recver = luvco.ispawn_r("")
do
    local ok, r = recver:recv();
    print ("recv from close coro, over", ok, r)
end