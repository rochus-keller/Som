-- generated by SomLjVirtualMachine on Sa. Aug. 29 23:05:07 2020

local metaclass = Sieve
local class = Sieve._class
local function _block(f) local t = { _f = f }; setmetatable(t,Block._class); return t end
local _str = _primitives._newString
local _sym = _primitives._newSymbol
local _dbl = _primitives._newDouble
local _cl = _primitives._checkLoad

function class.benchmark(self)
	local flags
	flags = (Array):new_((5000));
	return (self):sieve_size_(flags,(5000));
end

function class.verifyResult_(self,result)
	return (self):assert_equals_((669),result);
end

function class.sieve_size_(self,flags,size)
	local primeCount
	primeCount = (0);
	(flags):putAll_(true);
	((2)):to_do_(size,_block( function(i)
				return ((flags):at_((i):_0m((1)))):ifTrue_(_block( function()
			local k
			primeCount = (primeCount):_0p((1));
			k = (i):_0p(i);
						return (_block( function()
								return (k):_0lq(size)
			end )):whileTrue_(_block( function()
				(flags):at_put_((k):_0m((1)),false);
				k = (k):_0p(i)
				return k
			end ))
		end ))
	end ));
	return primeCount;
end

