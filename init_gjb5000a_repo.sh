#!/bin/bash
# 创建符合GJB5000A的仓库结构
mkdir -p {01_项目管理,02_需求,03_设计,04_实现,05_测试,06_交付件,07_配置管理}
for item in SDP SRS TDD STP SC EXE STR SUM SDSR; do
    mkdir -p $(find . -type d -name "*ZC_${item}*")
done

# 添加基础管控文件
echo "ZC_CM_PLAN" > 07_配置管理/配置管理计划.md
git init
git add .
git commit -m "初始化符合GJB5000A的配置项目录结构"
