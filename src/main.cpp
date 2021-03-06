#define COMPARE_2_OPENCV

#ifdef COMPARE_2_OPENCV
#include "opencv2/opencv.hpp"
#include <opencv2/calib3d.hpp>
using namespace cv;
#endif  //  COMPARE_2_OPENCV

#include <glog/logging.h>
#include <gflags/gflags.h>
#include <time.h>
#include <theia/theia.h>
#include <chrono>  // NOLINT
#include <string>
#include <vector>
#include <fstream>
#include "type.h"
#include "command_line_helpers.h"
#include "handeyecalibrationbuilder.h"
#include "handeyecalibration_utils.h"
using namespace std;

void cout_indented(int n_space, const string& str)
{
    if(n_space >= 0) std::cout << std::string(n_space * 2, ' ') << str << std::endl;
    }


// Input/output files.
DEFINE_string(images, "", "Wildcard of images to reconstruct.");
DEFINE_string(matches_file, "", "Filename of the matches file.");
DEFINE_string(calibration_file, "",
              "Calibration file containing image calibration data.");
DEFINE_int32(chessboard_nx, -1,
             "Number of grids in X direction of chessboard.");

DEFINE_int32(chessboard_ny, -1,
             "Number of grids in Y direction of chessboard.");
DEFINE_double(cell_mm, -1.0,
             "length of chessboard grid in millimeter.");

DEFINE_string(hand_poses_file, "",
              "hand poses file containing hand poses");
DEFINE_string(
    output_matches_file, "",
    "File to write the two-view matches to. This file can be used in "
    "future iterations as input to the reconstruction builder. Leave empty if "
    "you do not want to output matches.");
DEFINE_string(
    output_reconstruction, "",
    "Filename to write reconstruction to. The filename will be appended with "
    "the reconstruction number if multiple reconstructions are created.");

// Multithreading.
DEFINE_int32(num_threads, 1,
             "Number of threads to use for feature extraction and matching.");

// Feature and matching options.
DEFINE_string(
    descriptor, "SIFT",
    "Type of feature descriptor to use. Must be one of the following: "
    "SIFT");
DEFINE_string(feature_density, "NORMAL",
              "Set to SPARSE, NORMAL, or DENSE to extract fewer or more "
              "features from each image.");
DEFINE_string(matching_strategy, "BRUTE_FORCE",
              "Strategy used to match features. Must be BRUTE_FORCE "
              " or CASCADE_HASHING");
DEFINE_bool(match_out_of_core, true,
            "Perform matching out of core by saving features to disk and "
            "reading them as needed. Set to false to perform matching all in "
            "memory.");
DEFINE_string(matching_working_directory, "",
              "Directory used during matching to store features for "
              "out-of-core matching.");
DEFINE_int32(matching_max_num_images_in_cache, 128,
             "Maximum number of images to store in the LRU cache during "
             "feature matching. The higher this number is the more memory is "
             "consumed during matching.");
DEFINE_double(lowes_ratio, 0.8, "Lowes ratio used for feature matching.");
DEFINE_double(
    max_sampson_error_for_verified_match, 4.0,
    "Maximum sampson error for a match to be considered geometrically valid.");
DEFINE_int32(min_num_inliers_for_valid_match, 30,
             "Minimum number of geometrically verified inliers that a pair on "
             "images must have in order to be considered a valid two-view "
             "match.");
DEFINE_bool(bundle_adjust_two_view_geometry, true,
            "Set to false to turn off 2-view BA.");
DEFINE_bool(keep_only_symmetric_matches, true,
            "Performs two-way matching and keeps symmetric matches.");

// Reconstruction building options.
DEFINE_string(reconstruction_estimator, "GLOBAL",
              "Type of SfM reconstruction estimation to use.");
DEFINE_bool(reconstruct_largest_connected_component, false,
            "If set to true, only the single largest connected component is "
            "reconstructed. Otherwise, as many models as possible are "
            "estimated.");
DEFINE_bool(shared_calibration, false,
            "Set to true if all camera intrinsic parameters should be shared "
            "as a single set of intrinsics. This is useful, for instance, if "
            "all images in the reconstruction were taken with the same "
            "camera.");
DEFINE_bool(only_calibrated_views, false,
            "Set to true to only reconstruct the views where calibration is "
            "provided or can be extracted from EXIF");
DEFINE_int32(min_track_length, 2, "Minimum length of a track.");
DEFINE_int32(max_track_length, 50, "Maximum length of a track.");
DEFINE_string(intrinsics_to_optimize,
              "NONE",
              "Set to control which intrinsics parameters are optimized during "
              "bundle adjustment.");
DEFINE_double(max_reprojection_error_pixels, 4.0,
              "Maximum reprojection error for a correspondence to be "
              "considered an inlier after bundle adjustment.");

// Global SfM options.
DEFINE_string(global_rotation_estimator, "ROBUST_L1L2",
              "Type of global rotation estimation to use for global SfM.");
DEFINE_string(global_position_estimator, "NONLINEAR",
              "Type of global position estimation to use for global SfM.");
DEFINE_bool(refine_relative_translations_after_rotation_estimation, true,
            "Refine the relative translation estimation after computing the "
            "absolute rotations. This can help improve the accuracy of the "
            "position estimation.");
DEFINE_double(post_rotation_filtering_degrees, 5.0,
              "Max degrees difference in relative rotation and rotation "
              "estimates for rotation filtering.");
DEFINE_bool(extract_maximal_rigid_subgraph, false,
            "If true, only cameras that are well-conditioned for position "
            "estimation will be used for global position estimation.");
DEFINE_bool(filter_relative_translations_with_1dsfm, true,
            "Filter relative translation estimations with the 1DSfM algorithm "
            "to potentially remove outlier relativep oses for position "
            "estimation.");
DEFINE_int32(num_retriangulation_iterations, 1,
             "Number of times to retriangulate any unestimated tracks. Bundle "
             "adjustment is performed after retriangulation.");

// Nonlinear position estimation options.
DEFINE_int32(
    position_estimation_min_num_tracks_per_view, 0,
    "Minimum number of point to camera constraints for position estimation.");
DEFINE_double(position_estimation_robust_loss_width, 0.1,
              "Robust loss width to use for position estimation.");

// Triangulation options.
DEFINE_double(min_triangulation_angle_degrees, 4.0,
              "Minimum angle between views for triangulation.");
DEFINE_double(
    triangulation_reprojection_error_pixels, 15.0,
    "Max allowable reprojection error on initial triangulation of points.");
DEFINE_bool(bundle_adjust_tracks, true,
            "Set to true to optimize tracks immediately upon estimation.");

// Bundle adjustment parameters.
DEFINE_string(bundle_adjustment_robust_loss_function, "NONE",
              "By setting this to an option other than NONE, a robust loss "
              "function will be used during bundle adjustment which can "
              "improve robustness to outliers. Options are NONE, HUBER, "
              "SOFTLONE, CAUCHY, ARCTAN, and TUKEY.");
DEFINE_double(bundle_adjustment_robust_loss_width, 10.0,
              "If the BA loss function is not NONE, then this value controls "
              "where the robust loss begins with respect to reprojection error "
              "in pixels.");

using namespace std;
using theia::Reconstruction;
using theia::ReconstructionBuilder;
using theia::ReconstructionBuilderOptions;

// Sets the feature extraction, matching, and reconstruction options based on
// the command line flags. There are many more options beside just these located
// in //theia/vision/sfm/reconstruction_builder.h
ReconstructionBuilderOptions SetReconstructionBuilderOptions()
{
    ReconstructionBuilderOptions options;
    options.num_threads = FLAGS_num_threads;
    options.output_matches_file = FLAGS_output_matches_file;

    options.descriptor_type = StringToDescriptorExtractorType(FLAGS_descriptor);
    options.feature_density = StringToFeatureDensity(FLAGS_feature_density);
    options.matching_options.match_out_of_core = FLAGS_match_out_of_core;
    options.matching_options.keypoints_and_descriptors_output_dir =
        FLAGS_matching_working_directory;
    options.matching_options.cache_capacity =
        FLAGS_matching_max_num_images_in_cache;
    options.matching_strategy =
        StringToMatchingStrategyType(FLAGS_matching_strategy);
    options.matching_options.lowes_ratio = FLAGS_lowes_ratio;
    options.matching_options.keep_only_symmetric_matches =
        FLAGS_keep_only_symmetric_matches;
    options.min_num_inlier_matches = FLAGS_min_num_inliers_for_valid_match;
    options.matching_options.perform_geometric_verification = true;
    options.matching_options.geometric_verification_options
    .estimate_twoview_info_options.max_sampson_error_pixels =
        FLAGS_max_sampson_error_for_verified_match;
    options.matching_options.geometric_verification_options.bundle_adjustment =
        FLAGS_bundle_adjust_two_view_geometry;
    options.matching_options.geometric_verification_options
    .triangulation_max_reprojection_error =
        FLAGS_triangulation_reprojection_error_pixels;
    options.matching_options.geometric_verification_options
    .min_triangulation_angle_degrees = FLAGS_min_triangulation_angle_degrees;
    options.matching_options.geometric_verification_options
    .final_max_reprojection_error = FLAGS_max_reprojection_error_pixels;

    options.min_track_length = FLAGS_min_track_length;
    options.max_track_length = FLAGS_max_track_length;

    // Reconstruction Estimator Options.
    theia::ReconstructionEstimatorOptions& reconstruction_estimator_options =
        options.reconstruction_estimator_options;
    reconstruction_estimator_options.min_num_two_view_inliers =
        FLAGS_min_num_inliers_for_valid_match;
    reconstruction_estimator_options.num_threads = FLAGS_num_threads;
    reconstruction_estimator_options.intrinsics_to_optimize =
        StringToOptimizeIntrinsicsType(FLAGS_intrinsics_to_optimize);
    options.reconstruct_largest_connected_component =
        FLAGS_reconstruct_largest_connected_component;
    options.only_calibrated_views = FLAGS_only_calibrated_views;
    reconstruction_estimator_options.max_reprojection_error_in_pixels =
        FLAGS_max_reprojection_error_pixels;

    // Which type of SfM pipeline to use (e.g., incremental, global, etc.);
    reconstruction_estimator_options.reconstruction_estimator_type =
        StringToReconstructionEstimatorType(FLAGS_reconstruction_estimator);

    // Global SfM Options.
    reconstruction_estimator_options.global_rotation_estimator_type =
        StringToRotationEstimatorType(FLAGS_global_rotation_estimator);
    reconstruction_estimator_options.global_position_estimator_type =
        StringToPositionEstimatorType(FLAGS_global_position_estimator);
    reconstruction_estimator_options.num_retriangulation_iterations =
        FLAGS_num_retriangulation_iterations;
    reconstruction_estimator_options
    .refine_relative_translations_after_rotation_estimation =
        FLAGS_refine_relative_translations_after_rotation_estimation;
    reconstruction_estimator_options.extract_maximal_rigid_subgraph =
        FLAGS_extract_maximal_rigid_subgraph;
    reconstruction_estimator_options.filter_relative_translations_with_1dsfm =
        FLAGS_filter_relative_translations_with_1dsfm;
    reconstruction_estimator_options
    .rotation_filtering_max_difference_degrees =
        FLAGS_post_rotation_filtering_degrees;
    reconstruction_estimator_options.nonlinear_position_estimator_options
    .min_num_points_per_view =
        FLAGS_position_estimation_min_num_tracks_per_view;

    // Triangulation options (used by all SfM pipelines).
    reconstruction_estimator_options.min_triangulation_angle_degrees =
        FLAGS_min_triangulation_angle_degrees;
    reconstruction_estimator_options
    .triangulation_max_reprojection_error_in_pixels =
        FLAGS_triangulation_reprojection_error_pixels;
    reconstruction_estimator_options.bundle_adjust_tracks =
        FLAGS_bundle_adjust_tracks;

    // Bundle adjustment options (used by all SfM pipelines).
    reconstruction_estimator_options.bundle_adjustment_loss_function_type =
        StringToLossFunction(FLAGS_bundle_adjustment_robust_loss_function);
    reconstruction_estimator_options.bundle_adjustment_robust_loss_width =
        FLAGS_bundle_adjustment_robust_loss_width;
    return options;
}

void AddMatchesToReconstructionBuilder(
    ReconstructionBuilder* reconstruction_builder)
{
    // Load matches from file.
    std::vector<std::string> image_files;
    std::vector<theia::CameraIntrinsicsPrior> camera_intrinsics_prior;
    std::vector<theia::ImagePairMatch> image_matches;

    // Read in match file.
    theia::ReadMatchesAndGeometry(FLAGS_matches_file,
                                  &image_files,
                                  &camera_intrinsics_prior,
                                  &image_matches);

    // Add all the views. When the intrinsics group id is invalid, the
    // reconstruction builder will assume that the view does not share its
    // intrinsics with any other views.
    theia::CameraIntrinsicsGroupId intrinsics_group_id =
        theia::kInvalidCameraIntrinsicsGroupId;
    if (FLAGS_shared_calibration)
    {
        intrinsics_group_id = 0;
    }

    for (int i = 0; i < image_files.size(); i++)
    {
        reconstruction_builder->AddImageWithCameraIntrinsicsPrior(
            image_files[i], camera_intrinsics_prior[i], intrinsics_group_id);
    }

    // Add the matches.
    for (const auto& match : image_matches)
    {
        CHECK(reconstruction_builder->AddTwoViewMatch(match.image1,
                match.image2,
                match));
    }
}

void AddImagesToReconstructionBuilder(
    ReconstructionBuilder* reconstruction_builder, int n_sp)
{
    std::vector<std::string> image_files;
    CHECK(theia::GetFilepathsFromWildcard(FLAGS_images, &image_files))
            << "Could not find images that matched the filepath: " << FLAGS_images
            << ". NOTE that the ~ filepath is not supported.";

    CHECK_GT(image_files.size(), 0) << "No images found in: " << FLAGS_images;

    // Load calibration file if it is provided.
    std::unordered_map<std::string, theia::CameraIntrinsicsPrior>
    camera_intrinsics_prior;
    if (FLAGS_calibration_file.size() != 0)
    {
        cout_indented(n_sp + 1, "FLAGS_calibration_file : " + FLAGS_calibration_file);
        CHECK(theia::ReadCalibration(FLAGS_calibration_file,
                                     &camera_intrinsics_prior))
                << "Could not read calibration file.";
        //exit(0);          
    }

    // Add images with possible calibration. When the intrinsics group id is
    // invalid, the reconstruction builder will assume that the view does not
    // share its intrinsics with any other views.
    theia::CameraIntrinsicsGroupId intrinsics_group_id =
        theia::kInvalidCameraIntrinsicsGroupId;
    if (FLAGS_shared_calibration)
    {
        intrinsics_group_id = 0;
    }

    for (const std::string& image_file : image_files)
    {
        std::string image_filename;
        cout<<image_file<<endl;
        CHECK(theia::GetFilenameFromFilepath(image_file, true, &image_filename));

        const theia::CameraIntrinsicsPrior* image_camera_intrinsics_prior =
            FindOrNull(camera_intrinsics_prior, image_filename);
        if (image_camera_intrinsics_prior != nullptr)
        {
            CHECK(reconstruction_builder->AddImageWithCameraIntrinsicsPrior(
                      image_file, *image_camera_intrinsics_prior, intrinsics_group_id));
        }
        else
        {
            CHECK(reconstruction_builder->AddImage(image_file, intrinsics_group_id));
        }
    }

    // Extract and match features.
    CHECK(reconstruction_builder->ExtractAndMatchFeatures());
}

#ifdef COMPARE_2_OPENCV

vector<Mat> read_hand_poses_from_file(const string& hand_poses_file)
{
    vector<Mat> li_hand_pose;
    std::ifstream indata;
    indata.open(hand_poses_file);
    std::string line;
    //Poses handposes;
    uint count = 0;
    while (getline(indata, line))
    {

//  Pose

//  [r11 r12 r13 tx]
//  [r21 r22 r23 ty]
//  [r31 r32 r33 tz]
//  [0   0   0   1 ]

//  In handposes.txt

//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image 1
//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image 2
//  ...
//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image N

        std::stringstream lineStream(line);
        std::string cell;
        Pose pose = Pose::Identity(4,4);
        count = 0;
        while (std::getline(lineStream, cell, ','))
        {
            pose(count%3,count/3) = std::stod(cell);
            count++;
        }
        handposes.push_back(pose);
    }

    return li_hand_pose;
}    




// reference : https://answers.opencv.org/question/215449/wrong-result-in-calibratehandeye-function-answered/

Mat handeye_opencv(const vector<string>& li_fn, const string& hand_poses_file, const Size& patternsize, float cell_size, int n_sp)
{
    cout_indented(n_sp, "handeye_opencv START");
    vector<Mat> R_gripper2base, t_gripper2base;
    Mat pose_cam_2_gripper, rvec(3, 1, CV_32F), tvec(3, 1, CV_32F), cameraMatrix(3, 3, CV_32FC1);
    int num_images = li_fn.size();
    vector<double> distortionCoefficients(5);
    vector<Point3f> obj_points;
    vector<Point2f> centers;
    //Size patternsize(6, 8); //number of centers
    std::vector<Mat> R_target2cam, t_target2cam;
    for (int i = 0; i < patternsize.height; ++i)
        for (int j = 0; j < patternsize.width; ++j)
            obj_points.push_back(Point3f(float(j*cell_size), float(i*cell_size), 0.f));

    for (size_t iI = 0; iI < num_images; iI++)
    {
        Mat frame = imread(li_fn[iI]); //source image
        bool patternfound = findChessboardCorners(frame, patternsize, centers);
        if (patternfound)
        {
            drawChessboardCorners(frame, patternsize, Mat(centers), patternfound);
                                                                //imshow("window", frame);
                                                                            //int key = cv::waitKey(0) & 0xff;
                
            solvePnP(Mat(obj_points), Mat(centers), cameraMatrix, distortionCoefficients, rvec, tvec);
            Mat R;
            Rodrigues(rvec, R); // R is 3x3
            R_target2cam.push_back(R);
            t_target2cam.push_back(tvec);
            Mat T = Mat::eye(4, 4, R.type()); // T is 4x4
            T(Range(0, 3), Range(0, 3)) = R * 1; // copies R into T
            T(Range(0, 3), Range(3, 4)) = tvec * 1; // copies tvec into T
                
            cout << "T = " << endl << " " << T << endl << endl;

        }
        //cout << patternfound << endl;
        cout_indented(n_sp + 1, "iI : " + to_string(iI) + " / " + to_string(num_images) + " : patternfound = " + to_string(patternfound));
    }
   
    //Mat R_cam2gripper = (Mat_<float>(3, 3)), t_cam2gripper = (Mat_<float>(3, 1));
    Mat R_cam2gripper, t_cam2gripper;

    calibrateHandEye(R_gripper2base, t_gripper2base, R_target2cam, t_target2cam, R_cam2gripper, t_cam2gripper, CALIB_HAND_EYE_TSAI);

    cout_indented(n_sp, "handeye_opencv END");
    return pose_cam_2_gripper;
}    
#endif  //  COMPARE_2_OPENCV


int main(int argc, char *argv[])
{
    printf("argc b4 : %d\nargv b4 : ", argc);
    for(int i = 0; i < argc; i++) printf("%s ", argv[i]);    printf("\n");
    google::ParseCommandLineFlags(&argc, &argv, true);
    printf("argc after : %d\nargv after : ", argc);
    for(int i = 0; i < argc; i++) printf("%s ", argv[i]);    printf("\n");
    //exit(0);   
    google::InitGoogleLogging(argv[0]);
#ifdef COMPARE_2_OPENCV
    std::vector<std::string> image_files;
    CHECK(theia::GetFilepathsFromWildcard(FLAGS_images, &image_files))
            << "Could not find images that matched the filepath: " << FLAGS_images
            << ". NOTE that the ~ filepath is not supported.";
    CHECK_GT(image_files.size(), 0) << "No images found in: " << FLAGS_images;
    Mat pose_cam_2_gripper = handeye_opencv(image_files, FLAGS_hand_poses_file, Size(FLAGS_chessboard_nx, FLAGS_chessboard_ny), FLAGS_cell_mm, 1);
    exit(0);
#endif  //  COMPARE_2_OPENCV
#if 0    
    cout << "FLAGS_output_reconstruction.size() : " << FLAGS_output_reconstruction.size() << endl;
    cout << "FLAGS_output_reconstruction : " << FLAGS_output_reconstruction << endl;
    exit(0);
#endif    
    CHECK_GT(FLAGS_output_reconstruction.size(), 0)
            << "Must specify a filepath to output the reconstruction.";

    const ReconstructionBuilderOptions options =
        SetReconstructionBuilderOptions();

    std::ifstream indata;
    indata.open(FLAGS_hand_poses_file);
    std::string line;
    Poses handposes;
    uint count = 0;
    while (getline(indata, line))
    {

//  Pose

//  [r11 r12 r13 tx]
//  [r21 r22 r23 ty]
//  [r31 r32 r33 tz]
//  [0   0   0   1 ]

//  In handposes.txt

//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image 1
//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image 2
//  ...
//  r11 r21 r31 r12 r22 r32 r13 r23 r33 tx ty tz # for image N

        std::stringstream lineStream(line);
        std::string cell;
        Pose pose = Pose::Identity(4,4);
        count = 0;
        while (std::getline(lineStream, cell, ','))
        {
            pose(count%3,count/3) = std::stod(cell);
            count++;
        }
        handposes.push_back(pose);
    }



    Timer timer;
    timer.Reset();
    cout_indented(0, "BBB");
    HandEyeCalibrationBuilder reconstruction_builder(options);
    cout_indented(0, "CCC");   // exit(0);

    // If matches are provided, load matches otherwise load images.
    if (FLAGS_matches_file.size() != 0)
    {
        AddMatchesToReconstructionBuilder(&reconstruction_builder);
    }
    else if (FLAGS_images.size() != 0)
    {
        AddImagesToReconstructionBuilder(&reconstruction_builder, 0);
    }
    else
    {
        LOG(FATAL)
                << "You must specifiy either images to reconstruct or a match file.";
    }

    reconstruction_builder.SetHandPoses(&handposes);

    HandEyeTransformation handeyetrans;
    CHECK(reconstruction_builder.BuildHandEyeCalibration(&handeyetrans))
            << "Could not create a reconstruction.";

    cout<<"runtime: "<<timer.ElapsedTimeInSeconds()<<endl;
    std::unique_ptr<Reconstruction> reconstructions =  reconstruction_builder.GetReconstruction();
    const std::string output_file =
        theia::StringPrintf("%s", FLAGS_output_reconstruction.c_str());
    CHECK(theia::WriteReconstruction(*reconstructions, output_file))
            << "Could not write reconstruction to file.";

    Eigen::IOFormat fmt;
    fmt.precision = Eigen::FullPrecision;
    cout<<"Estimated hand-eye transform:"<<handeyetrans.GetHandEyeRotationAsRotationMatrix().format(fmt)<<std::endl<<handeyetrans.GetHandEyeTranslation().format(fmt)<<endl;
}
