#!/bin/bash
# -*- coding: utf-8 -*-
# Ubuntu 上で Linux バイナリのビルド
# sudo apt install build-essential clang libomp-dev libopenblas-dev

# Example 1: 全パターンのビルド
# build.sh

# Example 2: 指定パターンのビルド(-c: コンパイラ名, -e: エディション名, -t: ターゲット名)
# build.sh -c clang++ -e YANEURAOU_ENGINE_NNUE

# Example 3: 特定パターンのビルド(複数指定時はカンマ区切り、 -e, -t オプションのみワイルドカード使用可、ワイルドカード使用時はシングルクォートで囲む)
# build.sh -c clang++,g++-9 -e '*KPPT*,*NNUE*'

MAKE=make
MAKEFILE=Makefile
JOBS=`grep -c ^processor /proc/cpuinfo 2>/dev/null`
if [ "$JOBS" -gt 8 ]; then
  JOBS=8
fi

ARCHCPUS='*'
COMPILERS="clang++,g++"
EDITIONS='*'
OS='linux'
TARGETS='*'
EXTRA=''

while getopts a:c:e:o:t:x: OPT
do
  case $OPT in
    a) ARCHCPUS="$OPTARG"
      ;;
    c) COMPILERS="$OPTARG"
      ;;
    e) EDITIONS="$OPTARG"
      ;;
    o) OS="$OPTARG"
      ;;
    t) TARGETS="$OPTARG"
      ;;
    x) EXTRA="$OPTARG"
      ;;
  esac
done

set -f
IFS=, eval 'ARCHCPUSARR=($ARCHCPUS)'
IFS=, eval 'COMPILERSARR=($COMPILERS)'
IFS=, eval 'EDITIONSARR=($EDITIONS)'
IFS=, eval 'TARGETSARR=($TARGETS)'

pushd `dirname $0`
pushd ../source

ARCHCPUS=(
  ZEN1
  ZEN2
  ZEN3
  AVX512VNNI
  AVXVNNI
  AVX512
  AVX2
  SSE42
  SSE41
  SSSE3
  SSE2
  NO_SSE
  GRAVITON2
  M1
  OTHER
)

EDITIONS=(
  YANEURAOU_ENGINE_NNUE
  YANEURAOU_ENGINE_NNUE_HALFKP_VM_256X2_32_32
  YANEURAOU_ENGINE_NNUE_HALFKPE9
  YANEURAOU_ENGINE_NNUE_KP256
  YANEURAOU_ENGINE_DEEP_TENSOR_RT
  YANEURAOU_ENGINE_KPPT
  YANEURAOU_ENGINE_KPP_KKPT
  YANEURAOU_ENGINE_MATERIAL
  YANEURAOU_ENGINE_MATERIAL2
  YANEURAOU_ENGINE_MATERIAL3
  YANEURAOU_ENGINE_MATERIAL4
  YANEURAOU_ENGINE_MATERIAL5
  YANEURAOU_ENGINE_MATERIAL6
  YANEURAOU_ENGINE_MATERIAL7
  YANEURAOU_ENGINE_MATERIAL8
  YANEURAOU_ENGINE_MATERIAL9
#  YANEURAOU_ENGINE_MATERIAL10
  YANEURAOU_MATE_ENGINE
  TANUKI_MATE_ENGINE
  USER_ENGINE
)

TARGETS=(
  normal
  tournament
  evallearn
  gensfen
)

declare -A EDITIONSTR;
EDITIONSTR=(
  ["YANEURAOU_ENGINE_NNUE"]="YANEURAOU_ENGINE_NNUE"
  ["YANEURAOU_ENGINE_NNUE_HALFKP_VM_256X2_32_32"]="YANEURAOU_ENGINE_NNUE_HALFKP_VM_256X2_32_32"
  ["YANEURAOU_ENGINE_NNUE_HALFKPE9"]="YANEURAOU_ENGINE_NNUE_HALFKPE9"
  ["YANEURAOU_ENGINE_NNUE_KP256"]="YANEURAOU_ENGINE_NNUE_KP256"
  ["YANEURAOU_ENGINE_DEEP_TENSOR_RT"]="YANEURAOU_ENGINE_DEEP_TENSOR_RT"
  ["YANEURAOU_ENGINE_KPPT"]="YANEURAOU_ENGINE_KPPT"
  ["YANEURAOU_ENGINE_KPP_KKPT"]="YANEURAOU_ENGINE_KPP_KKPT"
  ["YANEURAOU_ENGINE_MATERIAL"]="YANEURAOU_ENGINE_MATERIAL"
  ["YANEURAOU_ENGINE_MATERIAL2"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=2"
  ["YANEURAOU_ENGINE_MATERIAL3"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=3"
  ["YANEURAOU_ENGINE_MATERIAL4"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=4"
  ["YANEURAOU_ENGINE_MATERIAL5"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=5"
  ["YANEURAOU_ENGINE_MATERIAL6"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=6"
  ["YANEURAOU_ENGINE_MATERIAL7"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=7"
  ["YANEURAOU_ENGINE_MATERIAL8"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=8"
  ["YANEURAOU_ENGINE_MATERIAL9"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=9"
  ["YANEURAOU_ENGINE_MATERIAL10"]="YANEURAOU_ENGINE_MATERIAL MATERIAL_LEVEL=10"
  ["YANEURAOU_MATE_ENGINE"]="YANEURAOU_MATE_ENGINE"
  ["TANUKI_MATE_ENGINE"]="TANUKI_MATE_ENGINE"
  ["USER_ENGINE"]="USER_ENGINE"
);

declare -A DIRSTR;
DIRSTR=(
  ["YANEURAOU_ENGINE_NNUE"]="NNUE"
  ["YANEURAOU_ENGINE_NNUE_HALFKP_VM_256X2_32_32"]="NNUE_HALFKP_VM"
  ["YANEURAOU_ENGINE_NNUE_HALFKPE9"]="NNUE_HALFKPE9"
  ["YANEURAOU_ENGINE_NNUE_KP256"]="NNUE_KP256"
  ["YANEURAOU_ENGINE_DEEP_TENSOR_RT"]="DEEP_TRT"
  ["YANEURAOU_ENGINE_KPPT"]="KPPT"
  ["YANEURAOU_ENGINE_KPP_KKPT"]="KPP_KKPT"
  ["YANEURAOU_ENGINE_MATERIAL"]="MaterialLv1"
  ["YANEURAOU_ENGINE_MATERIAL2"]="MaterialLv2"
  ["YANEURAOU_ENGINE_MATERIAL3"]="MaterialLv3"
  ["YANEURAOU_ENGINE_MATERIAL4"]="MaterialLv4"
  ["YANEURAOU_ENGINE_MATERIAL5"]="MaterialLv5"
  ["YANEURAOU_ENGINE_MATERIAL6"]="MaterialLv6"
  ["YANEURAOU_ENGINE_MATERIAL7"]="MaterialLv7"
  ["YANEURAOU_ENGINE_MATERIAL8"]="MaterialLv8"
  ["YANEURAOU_ENGINE_MATERIAL9"]="MaterialLv9"
  ["YANEURAOU_ENGINE_MATERIAL10"]="MaterialLv10"
  ["YANEURAOU_MATE_ENGINE"]="YaneuraOu_MATE"
  ["TANUKI_MATE_ENGINE"]="tanuki_MATE"
  ["USER_ENGINE"]="KomoringHeights"
);

declare -A FILESTR;
FILESTR=(
  ["YANEURAOU_ENGINE_NNUE"]="YaneuraOu_NNUE"
  ["YANEURAOU_ENGINE_NNUE_HALFKP_VM_256X2_32_32"]="YaneuraOu_NNUE_HALFKP_VM"
  ["YANEURAOU_ENGINE_NNUE_HALFKPE9"]="YaneuraOu_NNUE_HalfKPE9"
  ["YANEURAOU_ENGINE_NNUE_KP256"]="YaneuraOu_NNUE_KP256"
  ["YANEURAOU_ENGINE_DEEP_TENSOR_RT"]="YaneuraOu_Deep_TRT"
  ["YANEURAOU_ENGINE_KPPT"]="YaneuraOu_KPPT"
  ["YANEURAOU_ENGINE_KPP_KKPT"]="YaneuraOu_KPP_KKPT"
  ["YANEURAOU_ENGINE_MATERIAL"]="YaneuraOu_MaterialLv1"
  ["YANEURAOU_ENGINE_MATERIAL2"]="YaneuraOu_MaterialLv2"
  ["YANEURAOU_ENGINE_MATERIAL3"]="YaneuraOu_MaterialLv3"
  ["YANEURAOU_ENGINE_MATERIAL4"]="YaneuraOu_MaterialLv4"
  ["YANEURAOU_ENGINE_MATERIAL5"]="YaneuraOu_MaterialLv5"
  ["YANEURAOU_ENGINE_MATERIAL6"]="YaneuraOu_MaterialLv6"
  ["YANEURAOU_ENGINE_MATERIAL7"]="YaneuraOu_MaterialLv7"
  ["YANEURAOU_ENGINE_MATERIAL8"]="YaneuraOu_MaterialLv8"
  ["YANEURAOU_ENGINE_MATERIAL9"]="YaneuraOu_MaterialLv9"
  ["YANEURAOU_ENGINE_MATERIAL10"]="YaneuraOu_MaterialLv10"
  ["YANEURAOU_MATE_ENGINE"]="YaneuraOu_MATE"
  ["TANUKI_MATE_ENGINE"]="tanuki_MATE"
  ["USER_ENGINE"]="KomoringHeights"
);

set -f
for COMPILER in ${COMPILERSARR[@]}; do
  echo "* compiler: ${COMPILER}"
  CSTR=${COMPILER##*/}
  CSTR=${CSTR##*\\}
  for EDITION in ${EDITIONS[@]}; do
    for EDITIONPTN in ${EDITIONSARR[@]}; do
      set +f
      if [[ $EDITION == $EDITIONPTN ]]; then
        set -f
        echo "* edition: ${EDITION}"
        BUILDDIR=../build/${OS}/${DIRSTR[$EDITION]}
        mkdir -p ${BUILDDIR}
        for TARGET in ${TARGETS[@]}; do
          for TARGETPTN in ${TARGETSARR[@]}; do
            set +f
            if [[ $TARGET == $TARGETPTN ]]; then
              set -f
              echo "* target: ${TARGET}"
              for ARCHCPU in ${ARCHCPUS[@]}; do
                for ARCHCPUPTN in ${ARCHCPUSARR[@]}; do
                  set +f
                  if [[ $ARCHCPU == $ARCHCPUPTN ]]; then
                    set -f
                    echo "* archcpu: ${ARCHCPU}"
                    TGSTR=${FILESTR[$EDITION]}-${OS}-${CSTR}-${TARGET}-${ARCHCPU}
                    ${MAKE} -f ${MAKEFILE} clean YANEURAOU_EDITION=${EDITIONSTR[$EDITION]} ${EXTRA}
                    nice ${MAKE} -f ${MAKEFILE} -j${JOBS} ${TARGET} TARGET_CPU=${ARCHCPU} YANEURAOU_EDITION=${EDITIONSTR[$EDITION]} COMPILER=${COMPILER} TARGET=${BUILDDIR}/${TGSTR} ${EXTRA} >& >(tee ${BUILDDIR}/${TGSTR}.log) || exit $?
                    ${MAKE} -f ${MAKEFILE} clean YANEURAOU_EDITION=${EDITIONSTR[$EDITION]} ${EXTRA}
                    break
                  fi
                  set -f
                done
              done
              break
            fi
            set -f
          done
        done
        break
      fi
      set -f
    done
  done
done

popd
popd
