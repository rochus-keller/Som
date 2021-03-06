-- generated by SomLjVirtualMachine on Sa. Aug. 29 23:05:07 2020

local metaclass = BenchmarkHarness
local class = BenchmarkHarness._class
local function _block(f) local t = { _f = f }; setmetatable(t,Block._class); return t end
local _str = _primitives._newString
local _sym = _primitives._newSymbol
local _dbl = _primitives._newDouble
local _cl = _primitives._checkLoad

function class.benchmarkClass_(self,class)
	self[2] = class;
	return self
end

function class.printAll_(self,aBool)
	self[7] = aBool;
	return self
end

function class.maxRuntime_(self,seconds)
	(seconds):ifNotNil_(_block( function()
		self[6] = ((seconds):_0s((1000))):_0s((1000))
		return self[6]
	end ));
	return self
end

function class.numIterations_(self,anInt)
	self[3] = anInt;
	return self
end

function class.warmUp_(self,anInt)
	self[4] = anInt;
	return self
end

function class.total(self)
	return self[1];
end

function class.run_(self,args)
	local _nonLocal, _nlRes
	local _status, _pcallRes = pcall( function()
		(((args):length()):_0l((2))):ifTrue_(_block( function()
			_nlRes = (self):printUsage(); _nonLocal = true; error(_nlRes)
		end ));
		(self):initialize();
		(self):processArguments_(args);
		(self):runBenchmark();
		(self):printTotal();
		return self
	end )
	if _status then return _pcallRes elseif _nonLocal then return _nlRes else error(_pcallRes) end
end

function class.initialize(self)
	self[1] = (0);
	self[3] = (1);
	self[4] = (0);
	self[5] = (1);
	self[6] = nil;
	self[7] = true;
	return self
end

function class.printUsage(self)
	(_str("./som.sh -cp Smalltalk Examples/Benchmarks/BenchmarkHarness.som [benchmark] [num-iterations [warm-up [inner-iter]]]")):println();
	(_str("")):println();
	(_str("  benchmark      - benchmark class name (e.g., Queens, Fibonacci, Dispatch)")):println();
	(_str("  num-iterations - number of times to execute benchmark, default: 1")):println();
	(_str("  warm-up        - number of times to execute benchmark before measuring, default: 0")):println();
	(_str("  inner-iter     - number of times the benchmark is executed in an inner loop, ")):println();
	(_str("                   which is measured in total, default: 1")):println();
	return self
end

function class.processArguments_(self,args)
	(self):loadBenchmarkClass_((args):at_((2)));
	(((args):length()):_0g((2))):ifTrue_(_block( function()
		self[3] = ((args):at_((3))):asInteger();
				return (((args):length()):_0g((3))):ifTrue_(_block( function()
			self[4] = ((args):at_((4))):asInteger();
						return (((args):length()):_0g((4))):ifTrue_(_block( function()
				self[5] = ((args):at_((5))):asInteger()
				return self[5]
			end ))
		end ))
	end ));
	return self
end

function class.loadBenchmarkClass_(self,className)
	local sym
	local cls
	sym = (className):asSymbol();
	cls = (_cl("system")):load_(sym);
	(cls):ifNil_(_block( function()
				return (self):error_((_str("Failed loading benchmark: ")):_0c(className))
	end ));
	self[2] = cls;
	return self
end

function class.runBenchmark(self)
	local bench
	local result
	bench = (self[2]):new();
	(bench):oneTimeSetup();
	(((_str("Starting ")):_0p((bench):name())):_0p(_str(" benchmark ... "))):print();
	(self):doWarmup_(bench);
	result = (self):doRuns_(bench);
	self[1] = (self[1]):_0p(result);
	(self):reportBenchmark_result_(bench,result);
	(_str("")):println();
	return self
end

function class.doWarmup_(self,bench)
	local _nonLocal, _nlRes
	local _status, _pcallRes = pcall( function()
		local numIterationsTmp
		local printAllTmp
		local maxRuntimeTmp
		((self[4]):_0g((0))):ifFalse_(_block( function()
			(_str("")):println();
			_nlRes = self; _nonLocal = true; error(_nlRes)
		end ));
		numIterationsTmp = self[3];
		printAllTmp = self[7];
		maxRuntimeTmp = self[6];
		self[3] = self[4];
		self[7] = false;
		self[6] = nil;
		(_str(" warmup ...")):print();
		(self):doRuns_(bench);
		self[3] = numIterationsTmp;
		self[7] = printAllTmp;
		self[6] = maxRuntimeTmp;
		(_str(" completed.")):println();
		return self
	end )
	if _status then return _pcallRes elseif _nonLocal then return _nlRes else error(_pcallRes) end
end

function class.doRuns_(self,bench)
	local _nonLocal, _nlRes
	local _status, _pcallRes = pcall( function()
		local i
		local total
		i = (0);
		total = (0);
		(_block( function()
						return (i):_0l(self[3])
		end )):whileTrue_(_block( function()
			local startTime
			local endTime
			local runTime
			startTime = (_cl("system")):ticks();
			((bench):innerBenchmarkLoop_(self[5])):ifFalse_(_block( function()
								return (self):error_(_str("Benchmark failed with incorrect result"))
			end ));
			endTime = (_cl("system")):ticks();
			runTime = (endTime):_0m(startTime);
			(self[7]):ifTrue_(_block( function()
								return (self):print_run_(bench,runTime)
			end ));
			total = (total):_0p(runTime);
			i = (i):_0p((1));
			(self[6]):ifNotNil_(_block( function()
								return ((total):_0g(self[6])):ifTrue_(_block( function()
					self[3] = i;
					_nlRes = total; _nonLocal = true; error(_nlRes)
				end ))
			end ));
						return (_cl("system")):fullGC()
		end ));
		return total;
	end )
	if _status then return _pcallRes elseif _nonLocal then return _nlRes else error(_pcallRes) end
end

function class.reportBenchmark_result_(self,bench,result)
	((bench):name()):print();
	(_str(": iterations=")):print();
	(self[3]):print();
	(_str(" average: ")):print();
	((result):_0h(self[3])):print();
	(_str("us")):print();
	(_str(" total: ")):print();
	(result):print();
	(_str("us")):println();
	return self
end

function class.print_run_(self,bench,runTime)
	((bench):name()):print();
	(_str(": iterations=1")):print();
	(_str(" runtime: ")):print();
	(runTime):print();
	(_str("us")):println();
	return self
end

function class.printTotal(self)
	(((_str("Total Runtime: ")):_0p((self[1]):asString())):_0p(_str("us"))):println();
	return self
end

