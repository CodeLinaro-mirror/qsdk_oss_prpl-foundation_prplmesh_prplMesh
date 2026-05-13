# prplMesh cross build on prplOS



## Goals

1. cost reasonable building time, typically less than 25 minutes on runner with 8 threads
2. require minimal gitlab ci executor setting, ideally works on gitlab instance runner
3. be possible for developers to run locally using docker
4. be capable of specifying prplos revision for cross building job on ci web interface
5. get rid of building given prplos revision as an image, which is difficult to debug



## Usages

### set up a cross build runner

nothing special is needed, follow the gitlab guide to create a docker executor and bind to prplMesh project

big disk volume is required, ideally 1TB

cache volume is set by default when create gitlab docker executor

```toml
[[runners]]
  [runners.docker]
    volumes = ["/cache"]
```


refer to: [docker executor](https://docs.gitlab.com/runner/executors/docker/)


### varibales

```
CI_PRPLOS_COMMIT_OVERWRITE
can be set on gitlab ci web ui, to overwrite the prplos commit id for debugging purpose
it is not reproducible by definition

CI_TARGET_BOARD
set by job, supports {freedom, ospv2, mozart}

CI_CONSERVATIVE_BUILD
set by job or set on gitlab ci. when set false, job re-uses host tool and toolchain if possible
the re-using saves around 20 minutes of building time, and decrease the volume usage by around 4GB
set true by default (prefer correctness than speed)
host tool and toolchain are indexed by the combination of tools folder hash and toolchain folder hash
i.e. tools-12345678-mozart.tar and toolchain-12345678-mozart.tar.zst

CI_RUNNER_VOLUME_CLEAN_
set by job or set on gitlab ci. clean the whole volume on the working runner

CI_RUNNER_SLOT_CLEAN
set by job or set on gitlab ci. clean one given prplos build slot on the working runner
slot is indexed by the combination of prplos commit id and target, i.e. prplos-12345678-mozart
```



## Statistics

Statistics using 8 threads (or equivalent setting) docker environment as benchmark

"cold": the given prplos revision has not been built on the runner

"cold+!conservative": the given prplos revision has not been built on the runner, re-use host tool and toolchain if hash haven't change

"hot": the given prplos revision has been built successfully on the runner

| stages    | cold + !conservative | cold | hot  |
| --------- | -------------------- | ---- | ---- |
| config    | 2                    | 2    | 0    |
| host tool | 0                    | 12   | 0    |
| download  | 1                    | 4    | 0    |
| toolchain | 0                    | 8    | 0    |
| world     | 26                   | 25   | 0    |
| prplmesh  | 5                    | 5    | 5    |
| TOTAL     | 38                   | 58   | 5    |



| items      | disk usage                                                   | effect                                                       |
| ---------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| dl         | 1.2G minimal, increase gradually                             | shared by all jobs all the time, regardless of target and revision |
| toolchain  | around 290M per slot, three fast track platforms require 900M per hash | shared as long as the toolchain still usable, the toolchain folder merely change |
| host tool  | around 230M per slot, three fast track platforms require 700M per hash | shared as long as the host tool still usable, the tools folder merely change |
| ccache     | 5G maximal per platform, 15G in total for three fast track platforms | shared by all jobs all the time, on the same platform        |
| slot       | 10G in conservative mode, 5G in !conservative mode, per prplos  revision for one platform<br />note: ospv2 is different due to the problem of autoremove | shared for all jobs on given platform, as long as prplmesh is pointing to the prplos revision |
| estimation | assume prplmesh upstep frequency is less that three times per week<br />assume ci runs clean job every week end<br />120G in conservative mode<br />60G in !conservative mode |                                                              |

comparison to current solution, below are typical docker image size for one prplos revision on one platform
note: due to docker layer sharing, the in total size should be less than 96GB in this example

```
prplmesh-builder-whm-freedom:094b0309-2544915732     34.9GB
prplmesh-builder-whm-mozart:199f9e5e-2540534966      30.5GB
prplmesh-builder-whm-mozart:f4fd976c-2548592462      30.7GB
```



## Traps

Suggest to not spend time to "investigate" the traps, instead, simply remove the cache and re-run. reasons are:

* in hot build, server spent 6 minutes and failed, it is not an unbearable cost
* in cold build, failures are most likely due to the code, although there are some corner cases in this solution

to re-run the failed building job, either:

* set `CI_RUNNER_SLOT_CLEAN` to "yes" and run, that would be gentle enough to ci runner
* set `CI_RUNNER_VOLUME_CLEAN_` to "yes" and run again, runner removes everything, runner would not blame that

note that removing cache is HIGHLY recommended once the logic in `ci/crossbuild/ci-prplos-cross-build.sh` was modified



## TODO

* wrap the re-run logic in ci script and do it automatically
* ospv2 platform CONFIG_AUTOREMOVE problem
* epic: explore sdk and image builder