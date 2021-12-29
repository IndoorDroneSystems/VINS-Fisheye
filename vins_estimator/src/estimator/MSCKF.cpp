#include "MSCKF.hpp"


MSCKF::MSCKF() {
    Q_imu.setZero();
    Q_imu.block<3, 3>(0, 0) = GYR_N*Matrix3d::Identity();
    Q_imu.block<3, 3>(3, 3) = GYR_W*Matrix3d::Identity();

    Q_imu.block<3, 3>(6, 6) = ACC_N*Matrix3d::Identity();
    Q_imu.block<3, 3>(9, 9) = ACC_W*Matrix3d::Identity();

}

void MSCKF::predict(const double t, Vector3d _acc, Vector3d _gyro) {
    //Follows  Mourikis, Anastasios I., and Stergios I. Roumeliotis. 
    // "A multi-state constraint Kalman filter for vision-aided inertial navigation." 
    // Proceedings 2007 IEEE International Conference on Robotics and Automation. IEEE, 2007.
    // Sect III-B
    if (t_last < 0 ) {
        t_last = t;
    }

    double dt = t - t_last;

    static Vector3d acc_last = Vector3d(0.0, 0.0, 0.0);
    static Vector3d gyro_last = Vector3d(0,0,0);

    //trapezoidal integration
    Vector3d gyro = (_gyro + gyro_last)/2;
    Vector3d acc = (_gyro + gyro_last)/2;
    acc_last = _acc;
    gyro_last = _gyro;
    
    Vector3d angvel_hat = gyro - error_state.bias_gyro; //Planet angular velocity is ignored
    Matrix3d Rq_hat = nominal_state.get_imu_R();
    Vector3d acc_hat = acc - error_state.bias_acc;

    //Nominal State
    Quaterniond omg_l(0, angvel_hat.x(), angvel_hat.y(), angvel_hat.z());

    //Naive intergation
    auto qdot = nominal_state.q_imu * omg_l*dt;
    auto vdot = Rq_hat*acc_hat + G;
    auto pdot = nominal_state.v_imu;

    nominal_state.q_imu += qdot*dt;
    nominal_state.q_imu.normalize();
    nominal_state.v_imu += vdot*dt;
    nominal_state.p_imu += pdot*dt;


    //Error state
    // Model:
    // d (x_err)/dt = F_mat * x_err + G * n_imu
    // x_err: error state vector

    Eigen::Matrix<double, IMU_STATE_DIM, IMU_STATE_DIM> F_mat;
    F_mat.setZero();

    //Rows 1-3, dynamics on quat
    F_mat.block<3, 3>(0, 0) = skewSymmetric(angvel_hat);
    F_mat.block<3, 3>(0, 3) = - Matrix3d::Identity();

    //Rows 4-6, dynamics on bias is empty
    //Rows 7-9, dynamics on velocity
    F_mat.block<3, 3>(6, 0) = - Rq_hat * skewSymmetric(acc_hat);
    F_mat.block<3, 3>(6, 6) = - 2 * skewSymmetric(acc_hat);
    F_mat.block<3, 3>(6, 9) = - Rq_hat;
    F_mat.block<3, 3>(6, 12) = - skewSymmetric(acc_hat);
    //Rows 10-12, dynamics on bias is empty
    
    //Rows 13-15, dynamics on position
    F_mat.block<3, 3>(12, 6) = Matrix3d::Identity();

    Eigen::Matrix<double, IMU_STATE_DIM, IMU_NOISE_DIM> G_mat;
    G_mat.setZero();
    G_mat.block<3, 3>(0, 0) = - Matrix3d::Identity();
    G_mat.block<3, 3>(3, 3) = Matrix3d::Identity();
    G_mat.block<3, 3>(6, 6) = - Rq_hat;
    G_mat.block<3, 3>(9, 9) = Matrix3d::Identity();

    //Now we need to intergate this, naive approach is trapezoidal rule
    //x_err_new = F_mat*x_err_last*dt + x_err_last
    //Or \dot Phi = F_mat Phi, Phi(0) = I
    // Phi = I + F_mat Phi * dt
    Eigen::Matrix<double, IMU_STATE_DIM, IMU_STATE_DIM> Phi;
    Phi.setIdentity();
    Phi = Phi + F_mat*dt;
    auto G = G_mat*dt;
    
    // auto X_imu_new = Phi*error_state.get_imu_vector();
    // Suggest by (268)-(269) in Sola J. Quaternion kinematics for the error-state Kalman filter
    // We don't predict the error state space
    // Instead, we only predict the P of error state, and predict the nominal state
    auto P_new = Phi*error_state.get_imu_P()*Phi.transpose() + G*Q_imu*G.transpose();
    auto P_imu_other_new = Phi*error_state.get_imu_other_P();

    //Set states to error_state
    // error_state.set_imu_vector(X_imu_new);
    error_state.set_imu_P(P_new);
    error_state.set_imu_other_P(P_imu_other_new);
}

void MSCKF::add_keyframe(const double t) {
    //For convience, we require t here is exact same to last imu t
    if (t_last >= 0) {
        assert(fabs(t - t_last) < 1/IMU_FREQ && "MSCKF new image must be added EXACTLY after the corresponding imu is applied!");
    }

    error_state.add_keyframe(t);
    nominal_state.add_keyframe(t);
}

void MSCKF::update(const FeaturePerId & feature_by_id) {
    
}
