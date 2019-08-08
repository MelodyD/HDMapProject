//
// Created by joey on 19-8-5.
//

#ifndef DEECAMP_VO_H
#define DEECAMP_VO_H

#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/features2d/features2d.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/calib3d/calib3d.hpp>
#include "vo_extra.h" // used in opencv2
#include "consts.h"

using namespace std;
using namespace cv;

Point2f pixel2cam ( const Point2d& p, const Mat& K )
{
    return Point2f
            (
                    ( p.x - K.at<double>(0,2) ) / K.at<double>(0,0),
                    ( p.y - K.at<double>(1,2) ) / K.at<double>(1,1)
            );
}


//根据输入的两张mat图片，进行orb关键点选取和匹配（基于BRIEF描述子之间的距离）
void find_feature_matches ( const Mat& img_1, const Mat& img_2, vector<KeyPoint>& keypoints_1,
                            vector<KeyPoint>& keypoints_2, vector< DMatch >& good_matches ) {
    //-- 初始化
    Mat descriptors_1, descriptors_2;
    Ptr<FeatureDetector> detector = FeatureDetector::create ( "ORB" );
    Ptr<DescriptorExtractor> descriptor = DescriptorExtractor::create ( "ORB" );
    Ptr<DescriptorMatcher> matcher  = DescriptorMatcher::create("BruteForce-Hamming");

    //-- 第一步:检测 Oriented FAST 角点位置 并 根据角点位置计算BRIEF描述子
    detector->detect ( img_1, keypoints_1 );
    detector->detect ( img_2, keypoints_2 );
    descriptor->compute ( img_1, keypoints_1, descriptors_1 );
    descriptor->compute ( img_2, keypoints_2, descriptors_2 );

    //-- 第二步:对两幅图像中的BRIEF描述子进行匹配，使用 Hamming 距离
    vector<DMatch> matches;
    // BFMatcher matcher ( NORM_HAMMING );
    matcher->match ( descriptors_1, descriptors_2, matches ); //计算出来的match维度和descriptors的行数一致

    //-- 第三步:匹配点对筛选
    double min_dist=10000, max_dist=0;
    //找出所有匹配之间的最小距离和最大距离, 即是最相似的和最不相似的两组点之间的距离
    for ( int i = 0; i < descriptors_1.rows; i++ )
    {
        double dist = matches[i].distance;
        if ( dist < min_dist ) min_dist = dist;
        if ( dist > max_dist ) max_dist = dist;
    }
    printf ( "-- Max dist : %f \n", max_dist );
    printf ( "-- Min dist : %f \n", min_dist );
    //当描述子之间的距离大于两倍的最小距离时,即认为匹配有误.但有时候最小距离会非常小,设置一个经验值30作为下限.
    for ( int i = 0; i < descriptors_1.rows; i++ )
    {
        if ( matches[i].distance <= max ( 2*min_dist, 30.0 ) )
        {
            good_matches.push_back ( matches[i] );
        }
    }

    //-- 第四步:绘制匹配结果
    //cout << "good matches: " << good_matches.size() << endl;
    Mat img_match;
    Mat img_goodmatch;
    drawMatches ( img_1, keypoints_1, img_2, keypoints_2, matches, img_match );
    drawMatches ( img_1, keypoints_1, img_2, keypoints_2, good_matches, img_goodmatch );
    //imshow ( "所有匹配点对", img_match );
//    namedWindow("优化后匹配点对", 0);
    cvResizeWindow("优化后匹配点对", 1500, 1200);
    imshow ( "优化后匹配点对", img_goodmatch );
    //imwrite("../depth_results/vo/good_matches.png", img_goodmatch);
    waitKey(0);
}


// 这里有一些参数需要修改！！！
// 根据匹配到的matches, 将相对的R,t求出来。（此时的t是归一化之后得到的）
void pose_estimation_2d2d (const vector<KeyPoint>& keypoints_1, const vector<KeyPoint>& keypoints_2,
                           const vector< DMatch >& matches, Mat& R, Mat& t, Mat& K) {
    //-- 把匹配点转换为vector<Point2f>的形式
    vector<Point2f> points1;
    vector<Point2f> points2;
    for ( int i = 0; i < ( int ) matches.size(); i++ )
    {
        points1.push_back ( keypoints_1[matches[i].queryIdx].pt );
        points2.push_back ( keypoints_2[matches[i].trainIdx].pt );
    }

    //-- 计算基础矩阵
    Mat fundamental_matrix;
    fundamental_matrix = findFundamentalMat ( points1, points2, CV_FM_8POINT );
    cout<<"fundamental_matrix is "<<endl<< fundamental_matrix<<endl;

    //-- 计算本质矩阵
    Point2d principal_point ( K.at<double>(0, 2), K.at<double>(1, 2) );
//    double focal_length = (K.at<double>(0,0) + K.at<double>(1,1)) / 2;        					                                    //相机焦距, TUM dataset标定值
    Mat essential_matrix;
//    essential_matrix = findEssentialMat ( points1, points2, focal_length, principal_point );
    essential_matrix = findEssentialMat ( points1, points2, K.at<double>(0,0), K.at<double>(1,1), principal_point );
    cout<<"essential_matrix is "<<endl<< essential_matrix<<endl;

    //-- 计算单应矩阵
    Mat homography_matrix;
    homography_matrix = findHomography ( points1, points2, RANSAC, 3 );
    cout<<"homography_matrix is "<<endl<<homography_matrix<<endl;

    //-- 从本质矩阵中恢复旋转和平移信息.
//    recoverPose ( essential_matrix, points1, points2, R, t, focal_length, principal_point );
    recoverPose ( essential_matrix, points1, points2, R, t, K.at<double>(0,0), K.at<double>(1,1), principal_point );
    cout<<"R is "<<endl<<R<<endl;
    cout<<"t is "<<endl<<t<<endl;
}

//三角化计算深度, 但要考虑如何融入实际我们计算的R, t的绝对尺度。
void triangulation (const vector< KeyPoint >& keypoint_1, const vector< KeyPoint >& keypoint_2, const Mat& K,
                    const std::vector< DMatch >& matches, const Mat& T1, const Mat& T2, vector< Point3d >& points)
{
//    Mat T1 = (Mat_<float> (3,4) <<
//            1,0,0,0,
//            0,1,0,0,
//            0,0,1,0);
//    Mat T2 = (Mat_<float> (3,4) <<
//            R.at<double>(0,0), R.at<double>(0,1), R.at<double>(0,2), t.at<double>(0,0),
//            R.at<double>(1,0), R.at<double>(1,1), R.at<double>(1,2), t.at<double>(1,0),
//            R.at<double>(2,0), R.at<double>(2,1), R.at<double>(2,2), t.at<double>(2,0)
//    );

    vector<Point2f> pts_1, pts_2;
    for ( DMatch m:matches ) {
        // 将像素坐标转换至相机坐标
        pts_1.push_back ( pixel2cam( keypoint_1[m.queryIdx].pt, K) );
        pts_2.push_back ( pixel2cam( keypoint_2[m.trainIdx].pt, K) );
    }

    Mat pts_4d;
    cv::triangulatePoints( T1, T2, pts_1, pts_2, pts_4d );

    // 转换成非齐次坐标
    for ( int i=0; i<pts_4d.cols; i++ )
    {
        Mat x = pts_4d.col(i);
        x /= x.at<float>(3,0); // 归一化
        Point3d p (
                x.at<float>(0,0),
                x.at<float>(1,0),
                x.at<float>(2,0)
        );
        points.push_back( p );
    }
}

//    R1 = (Mat_<double> (3,3) <<
//            pose1.at<double>(0,0), pose1.at<double>(0,1), pose1.at<double>(0,2),
//            pose1.at<double>(1,0), pose1.at<double>(1,1), pose1.at<double>(1,2),
//            pose1.at<double>(2,0), pose1.at<double>(2,1), pose1.at<double>(2,2));
//    T1 = (Mat_<double> (3,4) <<
//            pose1.at<double>(0,0), pose1.at<double>(0,1), pose1.at<double>(0,2), pose1.at<double>(0,3) ,
//            pose1.at<double>(1,0), pose1.at<double>(1,1), pose1.at<double>(1,2), pose1.at<double>(1,3),
//            pose1.at<double>(2,0), pose1.at<double>(2,1), pose1.at<double>(2,2), pose1.at<double>(2,3));
//    T1 = K * T1;
//    T1 = (Mat_<double> (3,4) <<
//            R1.at<double>(0,0), R1.at<double>(0,1), R1.at<double>(0,2), pose1.at<double>(0,3),
//            R1.at<double>(1,0), R1.at<double>(1,1), R1.at<double>(1,2), pose1.at<double>(1,3),
//            R1.at<double>(2,0), R1.at<double>(2,1), R1.at<double>(2,2), pose1.at<double>(2,3));

//    cout << "T1: " << T1 << endl;
//    cout << "R1: " << R1 << endl;
    //第一种思路： 用8点法估计的R相乘得到R2, 再和pose2自己的t组成T2。
//    R2 = R * R1;
//    cout << "R2: " << R2 << endl;
//    T2 = (Mat_<double> (3,4) <<
//            R2.at<double>(0,0), R2.at<double>(0,1), R2.at<double>(0,2), pose2.at<double>(0,3),
//            R2.at<double>(1,0), R2.at<double>(1,1), R2.at<double>(1,2), pose2.at<double>(1,3),
//            R2.at<double>(2,0), R2.at<double>(2,1), R2.at<double>(2,2), pose2.at<double>(2,3));
//    //第二种思路： 直接给绝对的T2
//    T2 = (Mat_<double> (3,4) <<
//            pose2.at<double>(0,0), pose2.at<double>(0,1), pose2.at<double>(0,2), pose2.at<double>(0,3),
//            pose2.at<double>(1,0), pose2.at<double>(1,1), pose2.at<double>(1,2), pose2.at<double>(1,3),
//            pose2.at<double>(2,0), pose2.at<double>(2,1), pose2.at<double>(2,2), pose2.at<double>(2,3));
//    T2 = K * T2;
//    cout << "T2: " << T2 << endl;


#endif //DEECAMP_VO_H
