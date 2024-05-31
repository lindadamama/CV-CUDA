#!/bin/bash -e

# SPDX-FileCopyrightText: Copyright (c) 2022-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Usage: run_samples.sh
# Runs each sample of CV-CUDA one by one. Some are Python samples and some are C++.
# NOTE: This script may take a long time to finish since some samples may need to create
#	    TensorRT models as they run for the first time.

set -e

export CUDA_MODULE_LOADING="LAZY"
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
SAMPLES_DIR="$(dirname "$SCRIPT_DIR")"
CLASSIFICATION_OUT_DIR=/tmp/classification
SEGMENTATION_OUT_DIR="/tmp/segmentation"
DETECTION_OUT_DIR="/tmp/object_detection"
DISTANCE_LABEL_OUT_DIR="/tmp/distance_label"

echo "SAMPLES_DIR: $SAMPLES_DIR"
echo "CLASSIFICATION_OUT_DIR: $CLASSIFICATION_OUT_DIR"
echo "SEGMENTATION_OUT_DIR: $SEGMENTATION_OUT_DIR"
echo "DETECTION_OUT_DIR: $DETECTION_OUT_DIR"
echo "DISTANCE_LABEL_OUT_DIR: $DISTANCE_LABEL_OUT_DIR"

create_output_dir() {
    local base_dir=$1
    local run_number=1
    while [[ -d "$base_dir/$run_number" ]]; do
        let run_number++
    done
    mkdir -p "$base_dir/$run_number"
    echo "$base_dir/$run_number"
}

# Crop and Resize Sample
# Batch size 2
LD_LIBRARY_PATH=$SAMPLES_DIR/lib $SAMPLES_DIR/build/cropandresize/cvcuda_sample_cropandresize -i $SAMPLES_DIR/assets/images/ -b 2

# Run the classification Python sample. This will save the necessary TensorRT model
# and labels in the output directory. The C++ sample can then use those directly.
# Run the segmentation Python sample with default settings, without any command-line args.
rm -rf "$CLASSIFICATION_OUT_DIR"
mkdir "$CLASSIFICATION_OUT_DIR"
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -o "$CLASSIFICATION_RUN_DIR"
# Run it on a specific image with batch size 1 with PyTorch backend.
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -b 1 -bk pytorch -o "$CLASSIFICATION_RUN_DIR"
# # Run it on a specific image with batch size 4 with PyTorch backend. Uses Same image multiple times
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -b 4 -bk pytorch -o "$CLASSIFICATION_RUN_DIR"
# Run it on a folder worth of images with batch size 2 with PyTorch backend.
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/images/ -b 2 -bk pytorch -o "$CLASSIFICATION_RUN_DIR"
# Run it on a specific image with batch size 1 with TensorRT backend with saving the output in a specific directory.
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -b 1 -bk tensorrt -o "$CLASSIFICATION_RUN_DIR"
# Run it on a specific image with batch size 1 with TensorRT backend with saving the output in a specific directory.
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -b 2 -bk tensorrt -o "$CLASSIFICATION_RUN_DIR"
# Run it on a video with batch size 1 with TensorRT backend with saving the output in a specific directory.
CLASSIFICATION_RUN_DIR=$(create_output_dir "$CLASSIFICATION_OUT_DIR")
python3 $SAMPLES_DIR/classification/python/main.py -i $SAMPLES_DIR/assets/videos/pexels-ilimdar-avgezer-7081456.mp4 -b 1 -bk tensorrt -o "$CLASSIFICATION_RUN_DIR"

# Run the classification C++ sample. Since the Python sample was already run, we can reuse the TensorRT model
# and the labels file generated by it.
# Batch size 1
LD_LIBRARY_PATH=$SAMPLES_DIR/lib $SAMPLES_DIR/build/classification/cvcuda_sample_classification -e "$CLASSIFICATION_OUT_DIR/1/model.4.224.224.trtmodel" -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -l "$CLASSIFICATION_OUT_DIR/1/labels.txt" -b 1
# Batch size 2
LD_LIBRARY_PATH=$SAMPLES_DIR/lib $SAMPLES_DIR/build/classification/cvcuda_sample_classification -e "$CLASSIFICATION_OUT_DIR/1/model.4.224.224.trtmodel" -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -l "$CLASSIFICATION_OUT_DIR/1/labels.txt" -b 2

# Run the segmentation Python sample with default settings, without any command-line args.
rm -rf "$SEGMENTATION_OUT_DIR"
mkdir "$SEGMENTATION_OUT_DIR"
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -o "$SEGMENTATION_RUN_DIR"
# Run the segmentation sample with default settings for PyTorch backend.
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -bk pytorch -o "$SEGMENTATION_RUN_DIR"
# Run it on a single image with high batch size for the background class writing to a specific directory with PyTorch backend
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/images/tabby_tiger_cat.jpg -o "$SEGMENTATION_RUN_DIR" -b 5 -c __background__ -bk pytorch
# Run it on a folder worth of images with the default tensorrt backend
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/images/ -o "$SEGMENTATION_RUN_DIR" -b 4 -c __background__
# Run it on a folder worth of images with PyTorch
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/images/ -o "$SEGMENTATION_RUN_DIR" -b 5 -c __background__ -bk pytorch
# Run on a single image with custom resized input given to the sample for the dog class
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/images/Weimaraner.jpg -o "$SEGMENTATION_RUN_DIR" -b 1 -c dog -th 512 -tw 512
# Run it on a video for class background.
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/videos/pexels-ilimdar-avgezer-7081456.mp4 -b 4 -c __background__ -o "$SEGMENTATION_RUN_DIR"
# Run it on a video for class background with the PyTorch backend.
SEGMENTATION_RUN_DIR=$(create_output_dir "$SEGMENTATION_OUT_DIR")
python3 $SAMPLES_DIR/segmentation/python/main.py -i $SAMPLES_DIR/assets/videos/pexels-ilimdar-avgezer-7081456.mp4 -b 4 -c __background__ -bk pytorch -o "$SEGMENTATION_RUN_DIR"

# Run the object detection Python sample with default settings, without any command-line args.
rm -rf "$DETECTION_OUT_DIR"
mkdir "$DETECTION_OUT_DIR"
DETECTION_RUN_DIR=$(create_output_dir "$DETECTION_OUT_DIR")
python3 $SAMPLES_DIR/object_detection/python/main.py -o "$DETECTION_RUN_DIR"
# Run it with batch size 1 on a single image
DETECTION_RUN_DIR=$(create_output_dir "$DETECTION_OUT_DIR")
python3 $SAMPLES_DIR/object_detection/python/main.py -i $SAMPLES_DIR/assets/images/peoplenet.jpg  -b 1 -o "$DETECTION_RUN_DIR"
# Run it with batch size 4 on a video
DETECTION_RUN_DIR=$(create_output_dir "$DETECTION_OUT_DIR")
python3 $SAMPLES_DIR/object_detection/python/main.py -i $SAMPLES_DIR/assets/videos/pexels-chiel-slotman-4423925-1920x1080-25fps.mp4 -b 4 -o "$DETECTION_RUN_DIR"
# Run it with batch size 2 on a folder of images
DETECTION_RUN_DIR=$(create_output_dir "$DETECTION_OUT_DIR")
python3 $SAMPLES_DIR/object_detection/python/main.py -i $SAMPLES_DIR/assets/images/ -b 3 -o "$DETECTION_RUN_DIR"
# RUn it with the TensorFlow backend
DETECTION_RUN_DIR=$(create_output_dir "$DETECTION_OUT_DIR")
python3 $SAMPLES_DIR/object_detection/python/main.py -i $SAMPLES_DIR/assets/videos/pexels-chiel-slotman-4423925-1920x1080-25fps.mp4 -b 4 -bk tensorflow -o "$DETECTION_RUN_DIR"

# Run the distance label Python sample with default settings, without any command-line args.
rm -rf "$DISTANCE_LABEL_OUT_DIR"
mkdir "$DISTANCE_LABEL_OUT_DIR"
DISTANCE_LABEL_RUN_DIR=$(create_output_dir "$DISTANCE_LABEL_OUT_DIR")
python3 $SAMPLES_DIR/label/python/main.py -o "$DISTANCE_LABEL_RUN_DIR"
# Run it with batch size 1 on a single image
DISTANCE_LABEL_RUN_DIR=$(create_output_dir "$DISTANCE_LABEL_OUT_DIR")
python3 $SAMPLES_DIR/label/python/main.py -i $SAMPLES_DIR/assets/images/peoplenet.jpg  -b 1 -o "$DISTANCE_LABEL_RUN_DIR"
