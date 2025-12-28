总的来讲soa速度快一些，特别是在iteration的时候
但是churn的时候，不稳定，有时候会比aos还慢

所以真实使用的时候，还需要根据使用场景进行选择

然后pin cpu，关闭turbo可能有帮助

$ g++ -std=c++17 -g -O3 -march=native benchmark.cpp -o benchmark
$ ./benchmark
Fill to capacity (32768 orders)
  slow aos fill
    final depth: 32768
    time:        0.397368 ms
    ns/op:       12.1267
  fast soa fill
    final depth: 32768
    time:        0.444198 ms
    ns/op:       13.5558
  std::list fill
    final depth: 32768
    time:        1.3142 ms
    ns/op:       40.1063

Random erase from full depth (32768 cancels)
  slow aos erase
    final depth: 0
    time:        0.523505 ms
    ns/op:       15.9761
  fast soa erase
    final depth: 0
    time:        0.509218 ms
    ns/op:       15.5401
  std::list erase
    final depth: 0
    time:        1.00908 ms
    ns/op:       30.7947

Random erase/insert churn (200000 ops)
  slow aos churn
    final depth: 32696
    time:        3.66787 ms
    ns/op:       18.3393
  fast soa churn
    final depth: 32696
    time:        5.8007 ms
    ns/op:       29.0035
  std::list churn
    final depth: 32696
    time:        9.5301 ms
    ns/op:       47.6505

Pure iteration over full depth (2000 traversals)
  slow aos iterate
    final depth: 32768
    time:        142.12 ms
    ns/op:       71060
  fast soa iterate
    final depth: 32768
    time:        92.8879 ms
    ns/op:       46444
  std::list iterate
    final depth: 32768
    time:        133.497 ms
    ns/op:       66748.6

$ ./benchmark
Fill to capacity (32768 orders)
  slow aos fill
    final depth: 32768
    time:        0.35825 ms
    ns/op:       10.9329
  fast soa fill
    final depth: 32768
    time:        0.463269 ms
    ns/op:       14.1378
  std::list fill
    final depth: 32768
    time:        1.28394 ms
    ns/op:       39.1829

Random erase from full depth (32768 cancels)
  slow aos erase
    final depth: 0
    time:        0.469531 ms
    ns/op:       14.3289
  fast soa erase
    final depth: 0
    time:        0.419721 ms
    ns/op:       12.8089
  std::list erase
    final depth: 0
    time:        0.910225 ms
    ns/op:       27.7779

Random erase/insert churn (200000 ops)
  slow aos churn
    final depth: 32696
    time:        3.19525 ms
    ns/op:       15.9763
  fast soa churn
    final depth: 32696
    time:        9.83679 ms
    ns/op:       49.1839
  std::list churn
    final depth: 32696
    time:        6.87038 ms
    ns/op:       34.3519

Pure iteration over full depth (2000 traversals)
  slow aos iterate
    final depth: 32768
    time:        144.619 ms
    ns/op:       72309.4
  fast soa iterate
    final depth: 32768
    time:        92.9995 ms
    ns/op:       46499.7
  std::list iterate
    final depth: 32768
    time:        133.644 ms
    ns/op:       66822.2

