--[[
* Copyright 2020 Rochus Keller <mailto:me@rochus-keller.ch>
*
* This file is part of the SOM Smalltalk parser/compiler library.
*
* The following is the license that applies to this copy of the
* library. For a license to use the library under conditions
* other than those described here, please email to me@rochus-keller.ch.
*
* GNU General Public License Usage
* This file may be used under the terms of the GNU General Public
* License (GPL) versions 2.0 or 3.0 as published by the Free Software
* Foundation and appearing in the file LICENSE.GPL included in
* the packaging of this file. Please review the following information
* to ensure GNU General Public Licensing requirements will be met:
* http://www.fsf.org/licensing/licenses/info/GPLv2.html and
* http://www.gnu.org/copyleft/gpl.html.
]]--

local ffi = require 'ffi'
local C = ffi.C
local string = require 'string'
local io = require 'io'
local math = require 'math'
local bit = require 'bit'
local os = require 'os'

local module = {}

_primitives = module

module.C = ffi.C

ffi.cdef [[
	int Som_isWhiteSpace( const char* str );
	int Som_isLetters( const char* str );
	int Som_isDigits( const char* str );
	int Som_usecs();
]]

function module._newString(str)
	local t = { _str = str }
	setmetatable(t,String._class)
	return t
end
local _str = module._newString

function module._newSymbol(str)
	local t = { _sym = str }
	setmetatable(t,Symbol._class)
	return t
end
local _sym = module._newSymbol

function module._newDouble(d)
	local t = { _dbl = d }
	setmetatable(t,Double._class)
	return t
end
local _dbl = module._newDouble

function module._inst( cls )
	local t = {}
	setmetatable(t,cls._class)
	return t;
end

function module._checkLoad(name)
	return _G[name] -- TODO
end

---------- Object -------------------
module.Object = {}

function module.Object.class(self)
	if self._meta then
		return self._meta -- self is a class
	else
		return getmetatable(self) -- self is an instance
	end
end

function module.Object.objectSize(self)
	return 0 -- TODO
end

function module.Object.hashcode(self)
	return hashOf(self)
end

function module.Object.eqeq(self,other)
	if rawequal(self,other) then
		return true
	end
	-- TODO
end
module.Object["=="] = module.Object.eqeq

function module.Object.inspect(self)
	--TODO
end

function module.Object.halt(self)
	--TODO
end

function module.Object.perform_(self,aSymbol)
	return self[aSymbol._sym]()
end
module.Object["perform:"] = module.Object.perform_

function module.Object.perform_withArguments_(self,aSymbol,args)
	return self[aSymbol._sym](unpack(args)) 
end
module.Object["perform:withArguments:"] = module.Object.perform_withArguments_

function module.Object.perform_inSuperclass_(self,aSymbol,cls)
	return cls[aSymbol._sym]()
end
module.Object["perform:inSuperclass:"] = module.Object.perform_inSuperclass_

function module.Object.perform_withArguments_inSuperclass_(self,aSymbol,args,cls)
	return cls[aSymbol._sym](unpack(args)) 
end
module.Object["perform:withArguments:inSuperclass:"] = module.Object.perform_withArguments_inSuperclass_

function module.Object.instVarAt_(self,idx)
	return self[idx]
end
module.Object["instVarAt:"] = module.Object.instVarAt_

function module.Object.instVarAt_put_(self,idx,obj)
	self[idx] = obj
	return self
end
module.Object["instVarAt:put:"] = module.Object.instVarAt_put_

function module.Object.instVarNamed_(self,sym)
	print("instVarNamed: not implemented")
	TRAP()
end
module.Object["instVarNamed:"] = module.Object.instVarNamed_

---------- Class ----------------------
module.Class = {}

function module.Class.name(self)
	return _str(self._class._name)
end

function module.Class.new(self)
	local t = {}
	setmetatable(t,self._class)
	return t
end

function module.Class.superclass(self)
	return self._super
end

function module.Class.fields(self)
	local t = {}
	setmetatable(t,Array._class)
	local f = self._class._fields
	for i=1,#f do
		t[i] = _sym(f[i])
	end
	return t
end

function module.Class.methods(self)
	local t = {}
	setmetatable(t,Array._class)
	for k, v in pairs(self._class) do 
		if type(k) == "string" and type(v) == "function" then
			t[k] = v
		end
	end
	return t
end

---------- String ---------------------
module.String = {}

function module.String.concatenate_(self,argument)
	self._str = self._str .. argument._str
	return self
end
module.String ["concatenate:"] = module.String.concatenate_

function module.String.asSymbol(self)
	local t = { _sym = self._str }
	setmetatable( t, Symbol._class )
	return t
end

function module.String.hashcode(self)
	return hashOf(self)
end

function module.String.length(self)
	return string.len(self._str)
end

function module.String.isWhiteSpace(self)
	return C.Som_isWhiteSpace(self._str) == 1
end

function module.String.isLetters(self)
	return C.Som_isLetters(self._str) == 1
end

function module.String.isDigits(self)
	return C.Som_isDigits(self._str) == 1
end

function module.String.eq(self,argument)
	return self._str == argument._str
end
module.String["="] = module.String.eq

function module.String.primSubstringFrom_to_(self, start,_end)
	local t = { _str = string.sub(self._str,start,_end) }
	setmetatable( t, String._class )
	return t
end
module.String["primSubstringFrom:to:"] = module.String.primSubstringFrom_to_

---------- Symbol ---------------------
module.Symbol = {}

function module.Symbol.asString(self)
	local t = { _str = self._sym }
	setmetatable( t, String._class )
	return t
end

----------- Array ---------------------
module.Array = {}

function module.Array.at_(self,index)
	return self[index]
end
module.Array["at:"] = module.Array.at_

function module.Array.at_put_(self,index,value)
	self[index] = value
	return self
end
module.Array["at:put:"] = module.Array.at_put_

function module.Array.length(self)
	return self._n or #self
end

function module.Array.new_(self,length)
	local t = { _n = length }
	setmetatable( t, Array._class )
	return t
end
module.Array["^new:"] = module.Array.new_

---------- System ---------------------
module.System = {}

function module.System.printString(self,string)
	print( string._str )
	return self
end
module.System["printString:"] = module.System.printString

function module.System.global_(self,name)
	return _G[name]
end
module.System["global:"] = module.System.global_

function module.System.global_put_(self,name,value)
	_G[name] = value
	return self
end
module.System["global:put:"] = module.System.global_put_

function module.System.hasGlobal_(self,name)
	return _G[name] ~= nil
end
module.System["hasGlobal:"] = module.System.hasGlobal_

function module.System.load_(self,sym)
	-- TODO
	print("System.load not implemented")
	TRAP()
end
module.System["load:"] = module.System.load_

function module.System.exit_(self,err)
	-- ABORT()
	TRAP()
end
module.System["exit:"] = module.System.exit_

function module.System.printString_(self,str)
	io.stdout:write(str._str)
	return self
end
module.System["printString:"] = module.System.printString_

function module.System.printNewline(self)
	io.stdout:write("\n")
	return self
end
module.System["printNewline"] = module.System.printNewline

function module.System.time(self)
	return C.Som_usecs() / 1000 -- milliseconds since start
end
module.System["time"] = module.System.time

function module.System.ticks(self)
	return C.Som_usecs() -- microseconds since start
end
module.System["ticks"] = module.System.ticks

function module.System.fullGC(self)
	collectgarbage()
	return self
end
module.System["fullGC"] = module.System.fullGC

---------- Integer --------------------
module.Integer = {}

function module.Integer.plus(self,arg)
	return self + (-(-arg))
end
module.Integer["+"] = module.Integer.plus

function module.Integer.minus(self,arg)
	return self - (-(-arg))
end
module.Integer["-"] = module.Integer.minus

function module.Integer.star(self,arg)
	return self * (-(-arg))
end
module.Integer["*"] = module.Integer.star

function module.Integer.slash(self,arg)
	return math.floor( self / (-(-arg)) )
end
module.Integer["/"] = module.Integer.slash

function module.Integer.slashslash(self,arg)
	return _dbl( self / (-(-arg)) )
end
module.Integer["//"] = module.Integer.slashslash

function module.Integer.percent(self,arg)
	arg = (-(-arg))
	local res = self % arg
	if res ~= 0 and ( res < 0 ) ~= ( arg < 0) then 
        res = res + arg
    end
	return res
end
module.Integer["%"] = module.Integer.percent

function module.Integer.rem(self,arg)
	return self % (-(-arg))
end
module.Integer["rem:"] = module.Integer.rem

function module.Integer.ampers(self,arg)
	return bit.band(self,(-(-arg)))
end
module.Integer["&"] = module.Integer.ampers

function module.Integer.bitXor_(self,arg)
	return bit.xor(self,(-(-arg)))
end
module.Integer["bitXor:"] = module.Integer.bitXor_

function module.Integer.leftleft(self,arg)
	return bit.lshift(self,(-(-arg)))
end
module.Integer["<<"] = module.Integer.leftleft

function module.Integer.rightright(self,arg)
	return bit.rshift(self,(-(-arg)))
end
module.Integer[">>>"] = module.Integer.rightright

function module.Integer.sqrt(self)
	return math.sqrt(self)
end

function module.Integer.atRandom(self)
	return self * math.random()
end

function module.Integer.eq(self,arg)
	return self == (-(-arg))
end
module.Integer["="] = module.Integer.eq

function module.Integer.lt(self,arg)
	return self < (-(-arg))
end
module.Integer["<"] = module.Integer.lt

function module.Integer.asString(self)
	return _str(tostring(self))
end

function module.Integer.as32BitSignedValue(self)
	return math.floor(self)
end

function module.Integer.as32BitUnsignedValue(self)
	return bit.band(self,0xffffffff)
end

function module.Integer.fromString(self)
	return tonumber(self)
end
module.Integer["^fromString:"] = module.Integer.fromString

---------- Double ---------------------
module.Double = {}

-- requires Double to implement the __unm meta method

function module.Double.plus(self,arg)
	return _dbl(self._dbl + (-(-arg)))
end
module.Double["+"] = module.Double.plus

function module.Double.minus(self,arg)
	return _dbl(self._dbl - (-(-arg)))
end
module.Double["-"] = module.Double.minus

function module.Double.star(self,arg)
	return _dbl(self._dbl * (-(-arg)))
end
module.Double["*"] = module.Double.star

function module.Double.slashslash(self,arg)
	return _dbl(self._dbl / (-(-arg)))
end
module.Double["//"] = module.Double.slashslash

function module.Double.percent(self,arg)
	return _dbl(self._dbl % (-(-arg)))
end
module.Double["%"] = module.Double.percent

function module.Double.sqrt(self)
	return _dbl(math.sqrt(self._dbl))
end

function module.Double.round(self)
	return _dbl(math.floor(self._dbl + 0.5))
end

function module.Double.asInteger(self)
	return math.floor(self._dbl)
end

function module.Double.cos(self)
	return _dbl(math.cos(self._dbl))
end

function module.Double.sin(self)
	return _dbl(math.sin(self._dbl))
end

function module.Double.eq(self,arg)
	return self._dbl == arg._dbl
end
module.Double["="] = module.Double.eq

function module.Double.lt(self,arg)
	return self._dbl < (-(-arg))
end
module.Double["<"] = module.Double.lt

function module.Double.asString(self)
	return _str(tostring(self._dbl))
end

function module.Double.inf(self)
	return math.huge
end
module.Double["^PositiveInfinity"] = module.Double.inf

---------- Boolean --------------------
module.Boolean = {}

function module.Boolean.asString(self)
	return _str(tostring(self))
end

function module.Boolean.ifTrue(self,block)
	if self then
		block:value()
	end
end

module.Boolean["ifTrue:"] = module.Boolean.ifTrue

function module.Boolean.ifFalse(self,block)
	if not self then
		block:value()
	end
end
module.Boolean["ifFalse:"] = module.Boolean.ifFalse

function module.Boolean._not(self)
	return not self
end
module.Boolean["not"] = module.Boolean._not

function module.Boolean._or(self,block)
	if self then
		return true
	else
		return block:value()
	end
end
module.Boolean["or:"] = module.Boolean._or

function module.Boolean._and(self,block)
	if self then
		return block:value()
	else
		return false
	end
end
module.Boolean["and:"] = module.Boolean._and

--------------- Block --------------------
module.Block = {}

function module.Block.value(self)
	return self._f() -- no ':' here!
end

function module.Block.value_(self,argument)
	return self._f(argument)
end
module.Block["value:"] = module.Block.value_

function module.Block.value_with_(self,arg1,arg2)
	return self._f(arg1,arg2)
end
module.Block["value:with:"] = module.Block.value_with_

function module.Block.whileTrue_(self,block)
	while self:value() do
		block:value()
	end
end
module.Block["whileTrue:"] = module.Block.whileTrue_

---------------------------------------------------


return module
