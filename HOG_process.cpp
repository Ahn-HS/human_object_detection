#include "HOG_process.hpp"
#include "Parameters.hpp"
#include <math.h>
#include <string>
#include <iostream>
#include <iomanip>
#include <numeric>
#include <algorithm>
#include <stdexcept>
#include "opencv2/core/core.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "svm.h"

using namespace cv;
using namespace std;

HOG_process* HOG_process::inst = NULL;

const int HOG_process::PERSON_IN_IMAGE = 1;

const int HOG_process::NO_PERSON_IN_IMAGE = -1;

HOG_process* HOG_process::instance()
{
  if (NULL == HOG_process::inst)
  {
    HOG_process::inst = new HOG_process();
  }

  return HOG_process::inst;
}

void HOG_process::display(std::string fileName)
{
  cv::Mat image = imread(fileName.c_str());


  if (!image.data)
  {
    throw std::invalid_argument("Image could not be loaded");
  }
  else
  {
    imshow(fileName.c_str(),image);
  }
}

void HOG_process::train(std::vector<string> fileNames, std::vector<int> labels)
{
  double num_features;
  if (fileNames.size() < 1) 
  {
    throw std::invalid_argument("Must select one or more images to train");
  }
  if (labels.size() != fileNames.size()) 
  {
    throw std::invalid_argument("Number of labels must equal number of files");
  }

  for (unsigned int i=0;i < fileNames.size();i++)
  {
    cv::Mat image = imread(fileNames[i].c_str());
    if (!image.data)
    {
      throw std::invalid_argument("An image could not be loaded");
    }

    if ((labels[i] != HOG_process::PERSON_IN_IMAGE) && (labels[i] != HOG_process::NO_PERSON_IN_IMAGE))
    {
      throw std::invalid_argument("Invalid label");
    }
  }

  if (problem.l > 0)
  {
    cleanUpSvmModel();
  }

  // construct svm_problem
  problem.l = fileNames.size();
  problem.y = new double[labels.size()];
  problem.x = new struct svm_node*[fileNames.size()];

  for (int i = 0; i < problem.l;i++)
  {
    problem.y[i] = labels[i];
  }

  num_features = 0;

  for (int i = 0; i < problem.l;i++)
  {
    vector<float> descriptorValues;
    string fileName = fileNames[i];
    cv::Mat image = imread(fileName.c_str());

    retrieveDescriptors(image,descriptorValues,6,6,3,3);

    // we want the largest descriptor value to describe our number of
    // features
    if (num_features < descriptorValues.size())
    {
      num_features = descriptorValues.size();
    }

    vector<float>::const_iterator it;
    it = max_element(descriptorValues.begin(),descriptorValues.end());
    vector<struct svm_node*> nonzero_nodes;
    for (unsigned int j = 0; j < descriptorValues.size();j++)
    {
      if (descriptorValues[j] != 0)
      {
        struct svm_node *new_node = new struct svm_node;
        new_node->index = j;
        new_node->value = descriptorValues[j];
        nonzero_nodes.push_back(new_node);
      }
    }

    problem.x[i] = new struct svm_node[nonzero_nodes.size()+1];

    for (unsigned int j = 0;j < nonzero_nodes.size();j++)
    {
      problem.x[i][j].index = nonzero_nodes[j]->index;
      problem.x[i][j].value = nonzero_nodes[j]->value;
    }
    problem.x[i][nonzero_nodes.size()].index = -1;
    problem.x[i][nonzero_nodes.size()].value = -1;

    for (unsigned int j=0;j<nonzero_nodes.size();j++)
    {
      delete nonzero_nodes[j];
    }
  }

  const char *error_msg = svm_check_parameter(&problem, &parameters);
  if (NULL != error_msg)
  {
    throw std::logic_error(std::string("Error checking parameters " + std::string(error_msg)));
//"Error when checking Parameters");
  }

  // train the model
  model = svm_train(&problem, &parameters);
}

void HOG_process::calculateGradOrientation(const cv::Mat& gradX, const cv::Mat& gradY, cv::Mat& gradOrientation)
{
  float anOrientation;
  float gradYVal;
  float gradXVal;

  gradOrientation = cv::Mat::zeros(gradX.rows, gradX.cols, CV_32F);

  for (int i=0;i<gradOrientation.rows;i++)
  {
    for (int j=0;j<gradOrientation.cols;j++)
    {
      gradYVal = gradY.at<float>(i,j);
      gradXVal = gradX.at<float>(i,j);
      anOrientation = fastAtan2(gradYVal, gradXVal);

      if (anOrientation > 180.0)
      {
        anOrientation = anOrientation - 180.0;
      }

      gradOrientation.at<float>(i,j) = anOrientation;
    }
  }
}

void HOG_process::calculateGradMagnitude(const cv::Mat& gradX, const cv::Mat& gradY, cv::Mat& gradMagnitude)
{
  Mat gradXSquared = gradX.mul(gradX);
  Mat gradYSquared = gradY.mul(gradY);

  gradMagnitude = gradXSquared + gradYSquared;
  gradMagnitude.convertTo(gradMagnitude,CV_32F);
  sqrt(gradMagnitude, gradMagnitude);
}

void HOG_process::calculateGradients(const cv::Mat& image, cv::Mat& gradMag, cv::Mat& gradOrient)
{
  vector<cv::Mat> bgr_planes;
  float kernels[] = {-1.0,0.0,1.0};
  cv::Mat kernelX; cv::Mat kernelY;
  cv::Mat gradXB; cv::Mat gradYB;
  cv::Mat gradXG; cv::Mat gradYG;
  cv::Mat gradXR; cv::Mat gradYR;
  cv::Mat gradMagR; cv::Mat gradMagG; cv::Mat gradMagB;
  cv::Mat gradOrientR; cv::Mat gradOrientG; cv::Mat gradOrientB;

  gradMagR = cv::Mat(image.size(),CV_32F);
  gradMagG = cv::Mat(image.size(),CV_32F);
  gradMagB = cv::Mat(image.size(),CV_32F);
  gradMag = cv::Mat(image.size(),CV_32F);
  gradOrientR = cv::Mat(image.size(),CV_32F);
  gradOrientG = cv::Mat(image.size(),CV_32F);
  gradOrientB = cv::Mat(image.size(),CV_32F);
  gradOrient = cv::Mat(image.size(),CV_32F);

  kernelX = cv::Mat::ones(1,3,CV_32F);
  kernelX.at<float>(0) = kernels[0];
  kernelX.at<float>(1) = kernels[1];
  kernelX.at<float>(2) = kernels[2];

  kernelY = Mat::ones(3,1,CV_32F);
  kernelY.at<float>(0) = kernels[0];
  kernelY.at<float>(1) = kernels[1];
  kernelY.at<float>(2) = kernels[2];

  split(image,bgr_planes);

  filter2D(bgr_planes[0],gradXB,CV_32F,kernelX);
  filter2D(bgr_planes[0],gradYB,CV_32F,kernelY);
  filter2D(bgr_planes[1],gradXG,CV_32F,kernelX);
  filter2D(bgr_planes[1],gradYG,CV_32F,kernelY);
  filter2D(bgr_planes[2],gradXR,CV_32F,kernelX);
  filter2D(bgr_planes[2],gradYR,CV_32F,kernelY);

  calculateGradMagnitude(gradXR, gradYR, gradMagR); 
  calculateGradMagnitude(gradXG, gradYG, gradMagG); 
  calculateGradMagnitude(gradXB, gradYB, gradMagB); 
  calculateGradOrientation(gradXR, gradYR, gradOrientR); 
  calculateGradOrientation(gradXG, gradYG, gradOrientG); 
  calculateGradOrientation(gradXB, gradYB, gradOrientB); 

  for (int i=0;i < image.rows;i++)
  {
    for (int j=0;j < image.cols;j++)
    {
      float maxVal = gradMagB.at<float>(i,j);
      gradMag.at<float>(i,j) = gradMagB.at<float>(i,j);
      gradOrient.at<float>(i,j) = gradOrientB.at<float>(i,j);

      if (gradMagG.at<float>(i,j) > maxVal) 
      {
        maxVal = gradMagG.at<float>(i,j);
        gradMag.at<float>(i,j) = gradMagG.at<float>(i,j);
        gradOrient.at<float>(i,j) = gradOrientG.at<float>(i,j);
      }

      if (gradMagR.at<float>(i,j) > maxVal)
      { 
        gradMag.at<float>(i,j) = gradMagR.at<float>(i,j);
        gradOrient.at<float>(i,j) = gradOrientR.at<float>(i,j);
      }
    }
  }
}

void HOG_process::retrieveDescriptors(cv::Mat image, vector<float> &allDescriptorValues,int blockSizeX, int blockSizeY, int cellSizeX, int cellSizeY)
{
  int startY; int startX; int rangeY; int rangeX;
  vector<float> blockDescriptorValues;
  cv::Mat gradMag; cv::Mat gradOrient;

  calculateGradients(image,gradMag,gradOrient);

  allDescriptorValues.clear();

  rangeY=blockSizeY*cellSizeY;
  rangeX=blockSizeX*cellSizeX;
  startY = 0; 

  while(gradMag.rows > (startY+rangeY))
  {
    startX = 0;
    while (gradMag.cols > (startX+rangeX))
    {
      vector<float> blockDescriptors;
      retrieveDescriptorsFromBlock(gradMag, gradOrient, blockDescriptors, startY, startX, blockSizeX, blockSizeY, cellSizeX, cellSizeY);
      allDescriptorValues.insert(allDescriptorValues.end(),
        blockDescriptors.begin(), blockDescriptors.end());
      startX += rangeX;
    }
    startY += rangeY;
  }
}

void HOG_process::retrieveDescriptorsFromBlock(cv::Mat gradMag, cv::Mat gradOrient, vector<float> &allCellDescriptorValues,int startX,int startY, int blockSizeX, int blockSizeY, int cellSizeX, int cellSizeY)
{
//  int row; int col; 
int rangeX; int rangeY;
  allCellDescriptorValues.clear();
  allCellDescriptorValues.assign(9,0.0);

  rangeX=blockSizeX*cellSizeX;
  rangeY=blockSizeY*cellSizeY;

  for (int i=startY;i<rangeY;i+=cellSizeY)
  {
    for (int j=startX;j<rangeX;j+=cellSizeX)
    {
      vector<float> cellDescriptors;
      cellDescriptors.assign(9,0.0);
      retrieveDescriptorsFromCell(gradMag, gradOrient,cellDescriptors,j,i,cellSizeX,cellSizeY);
      for (unsigned int k=0; k < cellDescriptors.size();k++)
      {
        allCellDescriptorValues[k] = allCellDescriptorValues[k]
          + cellDescriptors[k];
      }
    }
  }

  float magnitude = 0.0;

  vector<float>::const_iterator iter;
  for (iter = allCellDescriptorValues.begin(); 
    iter != allCellDescriptorValues.end();iter++)
  {
    magnitude += pow((*iter),2);
  }

  if (magnitude != 0.0)
  {
    magnitude = sqrt(magnitude);
    for (unsigned int k = 0; k < allCellDescriptorValues.size();k++)
    {
      allCellDescriptorValues[k] = (allCellDescriptorValues[k] / magnitude) * 100;
    }
  }
}

void HOG_process::retrieveDescriptorsFromCell(cv::Mat gradMag, cv::Mat gradOrient, vector<float> &cellDescriptorValues, int startX, int startY, int cellSizeX, int cellSizeY)
{

  cellDescriptorValues.assign(9,0.0);

  for (int i=startY;i < (startY+cellSizeY);i++)
  {
    for (int j=startX;j < (startX+cellSizeX);j++)
    {
      if ((gradOrient.at<float>(i,j) >= 0.0) && (gradOrient.at<float>(i,j) < 20.0))
      {
        cellDescriptorValues[0] = cellDescriptorValues[0] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 20.0) && (gradOrient.at<float>(i,j) < 40.0))
      {
        cellDescriptorValues[1] = cellDescriptorValues[1] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 40.0) && (gradOrient.at<float>(i,j) < 60.0))
      {
        cellDescriptorValues[2] = cellDescriptorValues[2] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 60.0) && (gradOrient.at<float>(i,j) < 80.0))
      {
        cellDescriptorValues[3] = cellDescriptorValues[3] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 80.0) && (gradOrient.at<float>(i,j) < 100.0))
      {
        cellDescriptorValues[4] = cellDescriptorValues[4] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 100.0) && (gradOrient.at<float>(i,j) < 120.0))
      {
        cellDescriptorValues[5] = cellDescriptorValues[5] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 120.0) && (gradOrient.at<float>(i,j) < 140.0))
      {
        cellDescriptorValues[6] = cellDescriptorValues[6] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 140.0) && (gradOrient.at<float>(i,j) < 160.0))
      {
        cellDescriptorValues[7] = cellDescriptorValues[7] 
          + gradMag.at<float>(i,j);
      }
      else if ((gradOrient.at<float>(i,j) >= 160.0) && (gradOrient.at<float>(i,j) <= 180.0))
      {
        cellDescriptorValues[8] = cellDescriptorValues[8] 
          + gradMag.at<float>(i,j);
      }
    }
  }
}

void HOG_process::classify(const std::vector<string>& fileNames, const std::vector<int>& actualLabels, float& percentageCorrect, std::vector<int>& predictedLabels)
{
  if (fileNames.size() < 1) 
  {
    throw std::invalid_argument("Must select one or more images to classify");
  }

  if (actualLabels.size() != fileNames.size()) 
  {
    throw std::invalid_argument("Number of labels must equal number of files");
  }

  for (unsigned int i=0;i < fileNames.size();i++)
  {
    cv::Mat image = imread(fileNames[i].c_str());
    if (!image.data)
    {
      throw std::invalid_argument("An image could not be loaded");
    }
    if ((actualLabels[i] != HOG_process::PERSON_IN_IMAGE) && (actualLabels[i] != HOG_process::NO_PERSON_IN_IMAGE))
    {
      throw std::invalid_argument("Invalid label");
    }
  }

  if (NULL == model)
  {
    throw std::logic_error("Model not trained");
  }

  double predictedLabel; 

  std::vector<string>::const_iterator iter;
  for (iter = fileNames.begin();iter != fileNames.end();iter++)
  {
    vector<float> descriptorValues;
    struct svm_node *x;
    string fileName = (*iter);
    cv::Mat image = imread(fileName.c_str());

    retrieveDescriptors(image,descriptorValues,6,6,3,3);

    // build a sparse matrix of svm_nodes
    vector<struct svm_node*> nonzero_nodes;
    for (unsigned int j = 0; j < descriptorValues.size();j++)
    {
      if (descriptorValues[j] != 0)
      {
        struct svm_node *new_node = new struct svm_node;
        new_node->index = j;
        new_node->value = descriptorValues[j];
        nonzero_nodes.push_back(new_node);
      }
    }
    x = new struct svm_node[nonzero_nodes.size()+1];
    for (unsigned int j = 0;j < nonzero_nodes.size();j++)
    {
      x[j].index = nonzero_nodes[j]->index;
      x[j].value = nonzero_nodes[j]->value;
    }
    x[nonzero_nodes.size()].index = -1;
    x[nonzero_nodes.size()].value = -1;

    // clean up the temporary vector
    for (unsigned int j=0;j<nonzero_nodes.size();j++)
    {
      delete nonzero_nodes[j];
    }

    predictedLabel = svm_predict(model,x);
    predictedLabels.push_back(predictedLabel);
    delete x;
  }

  int numberCorrect;
  numberCorrect = 0;
  for (unsigned int i = 0;i<predictedLabels.size();i++)
  {
    if (predictedLabels[i] == actualLabels[i])
      numberCorrect++;
  }

  percentageCorrect = ((float) numberCorrect / predictedLabels.size())*100.0;
}

HOG_process::HOG_process()
{
  problem.l = 0;
  problem.y = NULL;
  problem.x = NULL;

  model = NULL;
}

HOG_process::~HOG_process()
{
}

void HOG_process::cleanUpSvmModel()
{
  if (problem.l > 0)
  {
    delete [] problem.y;

    for (int i = 0; i < problem.l; i++)
    {
      delete [] problem.x[i];
    }

    delete [] problem.x;
    problem.l = 0;
  }

  freeModelContent();
}

void HOG_process::freeModelContent()
{
  if (NULL != model)
  {
    svm_free_model_content(model);
    delete model;
    model = NULL;
  }
}
