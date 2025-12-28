fixed double

主要目的是用整数代替浮点数，提高计算速度，消除计算精度问题

一般有几种实现的方式
1. 按十进制缩放成整数，小数点的位置是固定的。 加减很快，乘除会慢一些，但是对于除1000，编译器会有优化，速度不会慢很多。 选这个方案，加减比double快，但是乘除慢。所以适合用在没有乘除的情况。并且做orderbook的时候，key是int稳定，也没有浮点数的误差问题。 为了加速，乘除法还不能考虑溢出，假定所有的数字都是合理的。 json解析的时候，不能通过double转，要直接转到int，否则会有误差，速度也慢
2. 按照二进制缩放成整数，小数点位置也是固定，优点是缩放的时候，直接移位就行，速度快。 缺点，因为二进制缩放的，所以无法准确表示某些小数的值，会有误差
3. 使用Q32.32的方式表示
4. 使用tick/lot size进行表示，对orderbook的比较适合，没有误差问题

主要是为了orderbook的使用


Build example:

```bash
g++ -std=c++20 -O3 -Wall -Wextra perf_compare.cpp -o perf_compare
./perf_compare
```

可能影响速度的参数有
-Ofast 这样编译器在double / 1000的时候会考虑转换成double * 0.001, 速度能提高很多。因为两者有细微差别，没有这个参数，编译器不会自动优化。
-march=native
-fno-tree-vectorize 不用vector并行计算了，速度会慢
-fno-unroll-loops 不展开loop了，速度可能会慢

taskset -c 2，指定cpu，希望抖动更小，但是实际上看下来抖动更大，不知道原因


`perf_compare` first runs a small correctness suite (round-trips, saturation, basic arithmetic) and then executes two benchmark groups:

可以看到没有溢出校验的加减快，但是普通的乘除慢很多
针对10的倍数的乘除，编译器有特别处理，速度快很多

所以最理想的应用场景是
1. 肯定不溢出的加减
2. 其次是偶尔会用到整数倍的除法
3. 如果大量使用普通的乘除，那完全没有速度优势了

$ ./perf_compare 
All FixedDouble checks passed
Arithmetic microbench (per op: 5000000 iterations)
  double add: 5.70625 ms, 1.14125 ns/op
  double sub: 5.62296 ms, 1.12459 ns/op
  double mul: 7.89842 ms, 1.57968 ns/op
  double div: 10.4403 ms, 2.08806 ns/op
  FixedDouble add: 3.94507 ms, 0.789013 ns/op
  FixedDouble sub: 3.48658 ms, 0.697315 ns/op
  FixedDouble mul: 60.2757 ms, 12.0551 ns/op
  FixedDouble div: 67.0817 ms, 13.4163 ns/op
sinks: 3.95051e+08 / 395048163939
Division vs reciprocal multiply (20000000 iterations)
  double: a/b: 41.5668 ms, 2.07834 ns/op
  double: a*(1/b): 28.0871 ms, 1.40436 ns/op
  double: a/1000: 39.454 ms, 1.9727 ns/op
  double: a*0.001: 21.8515 ms, 1.09257 ns/op
  FixedDouble: a/b: 254.999 ms, 12.7499 ns/op
  FixedDouble: a*(1/b): 243.362 ms, 12.1681 ns/op
  FixedDouble: a/1000: 23.7735 ms, 1.18868 ns/op
  FixedDouble: a*0.001: 38.5613 ms, 1.92807 ns/op
sinks: 109885 / 471951124961616557
