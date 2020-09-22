SOM=../SOM++

Benchmarks=( Richards Json Havlak Bounce List Mandelbrot NBody Permute Queens Sieve Storage Towers )
Iterations=( 20        20   20     150    150  50         1000   100     100    300   100     100     )

for i in "${!Benchmarks[@]}"
do
	echo "running" ${Benchmarks[i]} "with" ${Iterations[i]} "iterations"
	$SOM -cp .:Core:CD:DeltaBlue:Havlak:Json:NBody:Richards:Smalltalk Harness.som ${Benchmarks[i]} ${Iterations[i]} >> run.log
done


