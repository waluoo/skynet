local skynet = require "skynet"
local memory = require "memory"

memory.dumpinfo()
--memory.dump()
local info = memory.info()
for k,v in pairs(info) do
	print(string.format(":%08x %gK",k,v/1024))
end

print("Total memory:", memory.total())
print("Total block:", memory.block())
local num, size, maxc, minv = memory.ssinfo()
print("Short String number:", num)
print("Short String total size:", size)
print("Short String conflict:", maxc)
print("Short String min version", minv)

skynet.start(function() skynet.exit() end)
