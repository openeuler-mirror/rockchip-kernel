[English](./README.en.md) | 简体中文

#### 介绍

此项目的目的是移植 openEuler 内核以支持 Rockchip 开发板。处于开发中，欢迎提交贡献。

该仓库基于两个上游仓库 [openEuler 内核源码](https://gitee.com/openeuler/kernel) 和 [Rockchip 内核源码](https://github.com/rockchip-linux/kernel) .

#### 软件架构

Aarch64

#### 分支说明

- master：默认分支，暂时为空，存放入口信息。
- openEuler-20.03-LTS：存放 [openEuler 20.03 LTS 版本内核代码](https://gitee.com/openeuler/kernel/tree/openEuler-1.0-LTS/) 仓库的镜像，作为移植工作的基础代码。
- rockchip-develop-4.19：存放 [rockchip 内核 develop-4.19 分支代码](https://github.com/rockchip-linux/kernel/tree/develop-4.19) 仓库的镜像，作为移植工作的参考代码。
- dev-4.19：开发主分支，基于 openEuler 内核代码（linux 4.19 版本），开展支持 rockchip 开发板的内核移植工作。

#### 参与贡献

欢迎提交 PR，提交 PR 参考说明：https://gitee.com/openeuler/community/blob/master/CONTRIBUTING.md。

如果是针对 openEuler 社区内核主线的贡献，建议提交给 https://gitee.com/openeuler/kernel。
 