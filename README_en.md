> **Off World Live Fork:** This is a fork of [VRM4U](https://github.com/ruyo/VRM4U) with extended support for non-VRM skeletons (MetaHuman, DAZ, Mixamo) and live VMC mocap. See our [Wiki](https://github.com/Off-World-Live/VRM4U/wiki).
>
> **Fork features (UE 5.6):** live VMC mocap with automatic bone mapping, one-click IK Retargeter setup for MetaHuman and DAZ, a VMC-to-LiveLink face bridge for MetaHuman faces, a live tuning panel, and a Blueprint API. Full guide: **[`docs/vmc-guide.md`](docs/vmc-guide.md)**.

# VRM4U
Runtime VRM loader for UnrealEngine4

**For packaging, please download from UnrealEngine_VRM4UPlugin repository.**

## Description
VRM4U is importer for VRM.
Also it can load models on runtime.

[Document is here(I'm sorry, it's a google translation.)](https://translate.google.com/translate?um=1&ie=UTF-8&hl=ja&client=tw-ob&sl=ja&tl=en&u=https%3A%2F%2Fruyo.github.io%2FVRM4U%2F)

## Features
|||
|----|----|
|![2](https://github.com/ruyo/VRM4U/wiki/images/shot/03.png)|![2](https://github.com/ruyo/VRM4U/wiki/images/shot/04.png)|
|![2](https://github.com/ruyo/VRM4U/wiki/images/shot/01.png)|![2](https://github.com/ruyo/VRM4U/wiki/images/shot/02.png)|

- Import VRM file
- Animation
    - Generate bone, blendshape, swing bone, collision and humanoid rig.
    - Switch swing bone type PhysicsAsset/VRMSpringBone.
- Material
    - MToon simulated material. No postprocess.
- Mobile
    - Vanilla UE4Editor can use VRM on mobile by using BoneMap reduction.
    - Available on Forward/Deferred.

## Requirement
 - UE4.20 - UE4.27
 - Windows, Android, iOS, Mac(by ProjectBuild)
 - **For packaging, please download from UnrealEngine_VRM4UPlugin repository.**

## SampleMap
- VRM4UContent/Maps/VRM4U_sample.umap
![3](https://raw.githubusercontent.com/wiki/ruyo/VRM4U/images/samplemap.png)

## Usage
 - Drag and drop VRM file.

||
|----|
|![2](https://github.com/ruyo/VRM4U/wiki/images/overview.gif)|
|[![](https://img.youtube.com/vi/Qlz0bUSLjss/0.jpg)](https://www.youtube.com/watch?v=Qlz0bUSLjss) https://www.youtube.com/watch?v=Qlz0bUSLjss|


## Author
[@ruyo_h](https://twitter.com/ruyo_h)

## License

|||
|----|----|
|MIT|VRM4U|
|MIT|[JSON for Modern C++](https://github.com/nlohmann/json)|
|3-clause BSD-License|[assimp](https://github.com/assimp/assimp), [assimp](https://github.com/ruyo/assimp)|

### Source
https://github.com/ruyo/UnrealEngine_VRM4UPlugin ([Connecting Epic, GitHub Accounts](https://www.unrealengine.com/en-US/blog/updated-authentication-process-for-connecting-epic-github-accounts))

https://github.com/ruyo/assimp

Thanks.