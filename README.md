# README
前端bison、flex，后端llvm，直接光速实现一个编译器。
编译器其实很工程，只是工作层级比web后端更接近计算机底层而已。
如果你真的做过编译的项目，就知道这里面一样一堆dirty work，可能有时候比web后端还dirty，你可能一整天在那查某个segmentation fault到底咋回事。强烈建议大家祛魅。
segment fault太真实了，挨个pass去找
-mllvm -print-changed
其实难点在于解决小的技术突破，而并非实现一个已有的工具。比如malloc会形成PA的fragmentation，分配的时候怎么才会减少scatter，也降低page的负担。这还是open question，谁能解决别说写到简历里面，就是发paper都问题不大。可惜意识到很多这样的open question就很难了，提出真正的超越state of the art解决方案就更难了。相比之下还是抄书写个CPU更简单。
不管有多少dirty work、抄了多少别人的prototype 大学生实现一个编译器肯定比做一个商城难度大 哪怕是组件化的，但HR不会认的
能力 跟 视野，前者决定下限，后者决定上限