-- generated by SomLjVirtualMachine on Sa. Aug. 29 23:05:07 2020

local metaclass = Double
local class = Double._class
local function _block(f) local t = { _f = f }; setmetatable(t,Block._class); return t end
local _str = _primitives._newString
local _sym = _primitives._newSymbol
local _dbl = _primitives._newDouble
local _cl = _primitives._checkLoad

function class.abs(self)
	return ((self):_0l(_dbl(0.0))):ifTrue_ifFalse_((_dbl(0.0)):_0m(self),self);
end

function class.negated(self)
	return (_dbl(0.0)):_0m(self);
end

function class._0g(self,argument)
	return ((self):_0gq(argument)):and_(_block( function()
				return (self):_0lg(argument)
	end ));
end

function class._0gq(self,argument)
	return ((self):_0l(argument)):_not();
end

function class._0lq(self,argument)
	return ((self):_0l(argument)):or_(_block( function()
				return (self):_0q(argument)
	end ));
end

function class.negative(self)
	return (self):_0l(_dbl(0.0));
end

function class.between_and_(self,a,b)
	return ((self):_0g(a)):and_(_block( function()
				return (self):_0l(b)
	end ));
end

function class.to_do_(self,limit,block)
	local i
	i = self;
	(_block( function()
				return (i):_0lq(limit)
	end )):whileTrue_(_block( function()
		(block):value_(i);
		i = (i):_0p(_dbl(1.0))
		return i
	end ));
	return self
end

function class.downTo_do_(self,limit,block)
	local i
	i = self;
	(_block( function()
				return (i):_0gq(limit)
	end )):whileTrue_(_block( function()
		(block):value_(i);
		i = (i):_0m(_dbl(1.0))
		return i
	end ));
	return self
end

