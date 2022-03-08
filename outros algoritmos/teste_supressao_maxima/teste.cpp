// convert ground truth to IMU? https://stackoverflow.com/questions/60639665/visual-odometry-kitti-dataset
// this also can be useful: https://stackoverflow.com/questions/55756530/format-of-kitti-poses-dataset-poses-and-how-re-create-using-imu
//rename files https://ostechnix.com/how-to-rename-multiple-files-at-once-in-linux/
#include <ctype.h>
#include <algorithm>
#include <iterator>
#include <vector>
#include <ctime>
#include <sstream>
#include <fstream>
#include <string>
#include <opencv2/opencv.hpp>

/**
 ***************** Algorithm Outline
    1. Capture images: It, It+1,
    2. Undistort the above images. (using kitti so this step is done for you!)
    3. Use FAST algorithm to detect features in It, and track those features to It+1. A new detection is triggered if the number of features drop below a certain threshold.
    4. Use Nister’s 5-point alogirthm with RANSAC to compute the essential matrix.
    5. Estimate R,t from the essential matrix that was computed in the previous step.
    6. Take scale information from some external source (like a speedometer), and concatenate the translation vectors, and rotation matrices.
 *************************/

 using namespace std;
 using cv::Mat;
 using cv::Point2f;
 using cv::KeyPoint;
 using cv::Size;
 using cv::TermCriteria;

#define MAX_FRAME 7000
#define MIN_NUM_FEAT 2000

//const static char* dataset_images_location = "/home/matheus-ubuntu/Desktop/sequences/00/image_1";
 const static char* dataset_images_location = "/home/matheus-ubuntu/Desktop/street";
const static char* dataset_poses_location = "/home/matheus-ubuntu/Desktop/dataset/poses/01.txt";


vector<Point2f> getGreyCamGroundPoses() {
  string line;
  int i = 0;
  ifstream myfile (dataset_poses_location);
  double value = 0;
  vector<Point2f> poses;
  if (myfile.is_open())
  {
    while ( getline (myfile,line)  )
    {
      Point2f pose;
      std::istringstream in(line);
      for (int j=0; j<12; j++)  { //cada linha tem 12 elementos
        in >> value; // armazena em value o valor de in. (Stream a number till white space is encountered)
        if (j==11) pose.y=value; // store what is supposed to be tz. why? idk
        if (j==3) pose.x=value; //store tx. why? idk
      }

      poses.push_back(pose);
      i++;
    }
    myfile.close();
  }

  return poses;

}

vector<double> getAbsoluteScales()	{

  vector<double> scales;
  string line;
  int i = 0;
  ifstream myfile (dataset_poses_location);
  double x =0, y=0, z = 0;
  double x_prev, y_prev, z_prev;
  if (myfile.is_open())
  {
    while ( getline (myfile,line)  )
    {
      z_prev = z;
      x_prev = x;
      y_prev = y;
      std::istringstream in(line);
      //cout << line << '\n';
      for (int j=0; j<12; j++)  {
        in >> z ; //z recebe cada um dos 12 elementos do frame (linha) atual
        if (j==7) y=z; //se ty, y = ty
        if (j==3)  x=z; //se tx, x = tx
      }
      // z = tz
      scales.push_back(sqrt((x-x_prev)*(x-x_prev) + (y-y_prev)*(y-y_prev) + (z-z_prev)*(z-z_prev))); // raiz quadrada da soma do deslocamento em cada um dos três eixos ao quadrado
    }
    myfile.close();
  }

  return scales;
}

 void featureTracking(Mat img_1, Mat img_2, vector<Point2f>& points1, vector<Point2f>& points2, vector<uchar>& status){
   //this function automatically gets rid of points for which tracking fails

   vector<float> err;
   Size winSize(21,21);
   //condição de parada de uma função iterativa. termcrit(tipo de criterio, maxCount, epsilon)
   //tipo de criterio: COUNT: maximo numero de iterações e EPS: acuracia desejada
   TermCriteria termcrit(TermCriteria::COUNT+TermCriteria::EPS, 30, 0.01);

   // calcula o fluxo otico para os pontos selecionados (keyframes). points2 armazena a nova posição dos pontos na imagem 2
   //status é um array. se o ponto foi localizado na 2a imagem, seta 1. 0, caso contrario
   //array err: cada ponto encontrado na 2a imagem eh associado a um respectivo erro. Se o ponto não foi achado, erro não é definido
   //winsize: define tamanho da janela de procura
   // 3 eh o max level: são utilizados no maximo 4 niveis de piramide
   // 0.001: se o mínimo autovalor do fluxo de um ponto dividido pelo numero de pixels em uma image é menor que 0.001, então o ponto
   // correspondente eh descartado -> bad points removed    
   calcOpticalFlowPyrLK(img_1, img_2, points1, points2, status, err, winSize, 3, termcrit, 0, 0.001);

   //getting rid of points for which the KLT tracking failed or those who have gone outside the frame
   int indexCorrection = 0;
   //a função abaixo elimina do vetor os pontos não correspondidos na imagem 2.
   for( int i=0; i<status.size(); i++)
   {
     Point2f pt = points2.at(i- indexCorrection);
     if ((status.at(i) == 0) || (pt.x<0) || (pt.y<0))	{
       if((pt.x<0) || (pt.y<0))	{
         status.at(i) = 0;
       }

       points1.erase (points1.begin() + i - indexCorrection);
       points2.erase (points2.begin() + i - indexCorrection);
       indexCorrection++;
     }
   }
 }

 void featureDetection(Mat img, vector<Point2f> & points){ //retorna keypoints como vetor de inteiros
  vector<KeyPoint> keypoints; //keypoints detected on the image.
  int fast_threshold = 20; //threshold on difference between intensity of the central pixel and pixels of a circle around this pixel.
  bool nonmaxSupression = true; 

  cv::FAST(img, keypoints, fast_threshold, nonmaxSupression);
  KeyPoint::convert(keypoints, points, vector<int>());
  cout<<"number of features keypoints" <<keypoints.size()<<endl;
  cout<<"number of features points" <<points.size()<<endl;
 }


int main(int argc, char** argv) {

  bool renderFeatures = false;
  if (argc > 1) {
    renderFeatures = true;
  }

  Mat img_1, img_2;
  Mat R_f, t_f;


  double scale = 1.00;
  char filename1[200];
  char filename2[200];
  sprintf(filename1, "%s/%06d.png", dataset_images_location, 1);  //fornece o caminho completo pra primeira imagem: .../000000.png
  //cout<<"teste: "<<filename1<<endl;
  sprintf(filename2, "%s/%06d.png", dataset_images_location, 2);  //fornece o caminho completo pra segunda imagem: .../000001.png
  //cout<<"teste2: "<<filename2<<endl;

  char text[100];
  int fontFace = cv::FONT_HERSHEY_PLAIN;
  double fontScale = 1;
  int thickness = 1;
  cv::Point textOrg(10, 50);

  //read the first two frames from the dataset
  Mat img_1_c = cv::imread(filename1);                          //fornece matriz com as intensidades dos pixel em RGB(?)
  //cout<<"teste: "<<img_1_c<<endl;     
  Mat img_2_c = cv::imread(filename2);

  if ( !img_1_c.data || !img_2_c.data ) {
    std::cout<< " --(!) Error reading images " << std::endl; return -1;
  }

  // we work with grayscale images
  cvtColor(img_1_c, img_1, cv::COLOR_BGR2GRAY);
  //cv::imwrite(filename1, img_1);
  cvtColor(img_2_c, img_2, cv::COLOR_BGR2GRAY);
  //cv::imwrite(filename2, img_2);
  // feature detection, tracking
  vector<Point2f> points1, points2;        //vectors to store the coordinates of the feature points
  featureDetection(img_1, points1);        //detect features in img_1 e retorna keypoints como vetor de inteiros em points1
  //cout<<"teste: "<<points1<<endl;
  vector<uchar> status;
  featureTracking(img_1,img_2,points1,points2, status); //track those features to img_2
  //em points2 esta armazenados as posições dos pontos que possuem correspondencia com a imagem 1. apenas os pontos com boa performance


  //TODO: add a fucntion to load these values directly from KITTI's calib files
  // WARNING: different sequences in the KITTI VO dataset have different intrinsic/extrinsic parameters
  /*
   * Projection matrix example after rectification: Right Grey scale Camera (3x4)
   *
   *     7.188560000000e+02 0.000000000000e+00 6.071928000000e+02 -3.861448000000e+02
   *     0.000000000000e+00 7.188560000000e+02 1.852157000000e+02  0.000000000000e+00
   *     0.000000000000e+00 0.000000000000e+00 1.000000000000e+00  0.000000000000e+00
   */
  //double focal = 718.856; //focal lenght
  //cv::Point2d pp(607.1928, 185.2157); // pp is principal points from the camera. a imagem eh 1241x376
  //double focal = 1081.2352934517869; //redmi9
  //cv::Point2d pp(639.5, 359.5); //redmi9
  double focal = 1220.0899121457219; //iphone
  cv::Point2d pp(399.5, 179.5); //1280x720 eh a imagem minha

  //recovering the pose and the essential matrix
  Mat E, R, t, mask;
  //calcula a matriz essencial(essential for calibrated cameras and fundamental for uncalibrated cameras) dos pontos correspondentes em 2 imagens
  // RANSAC eh o metodo para calcular a matriz fundamental
  //nivel de confiança: 0.999
  // 1.0 eh o threshold: Parameter used for RANSAC. It is the maximum distance from a point to an epipolar line in pixels, 
  //beyond which the point is considered an outlier and is not used for computing the final fundamental matrix.  
  // mask: array com os N points. 0 para outliers e 1 para 'other points'
  E = findEssentialMat(points2, points1, focal, pp, cv::RANSAC, 0.999, 1.0, mask);
  //Recovers the relative camera rotation and the translation from an estimated essential matrix and the corresponding points in two images, 
  //using cheirality check. Returns the number of inliers that pass the check. 
  //provides R(output rotation matrix), t(output translation vector)
  //mask is input/output: Only these inliers will be used to recover pose. In the output mask only inliers which pass the cheirality check.
  recoverPose(E, points2, points1, R, t, focal, pp, mask);


  Mat prevImage = img_2;
  Mat currImage;
  vector<Point2f> prevFeatures = points2;
  vector<Point2f> currFeatures;

  char filename[100];

  R_f = R.clone();
  t_f = t.clone();

  clock_t begin = clock();

  cv::namedWindow( "Road facing camera | Top Down Trajectory", cv::WINDOW_NORMAL );// Create a window for display.
  cv::resizeWindow("Road facing camera | Top Down Trajectory", 600,600); //Serve para poder ajustar o tamanho da visualização
  // create a matrix 600x1241 of zeros
  //Mat traj = Mat::zeros(600, 1241, CV_8UC3); // CV_8UC3: 8-bit unsigned integer matrix with 3 channels, in other words, RGB
  Mat traj = Mat::zeros(600, 800, CV_8UC3); //redmi9
  auto groundPoses = getGreyCamGroundPoses(); //groundPoses eh um vetor 2d que recebe a translação em x e z entre cada frame
  auto groundScales = getAbsoluteScales(); //groundScales em um vetor com o deslocamento absoluto entre cada frame

  // aqui começa de fato o loop
  for(int numFrame=3; numFrame < 85; numFrame++) {
    //cout<<numFrame<<endl; 
    sprintf(filename, "%s/%06d.png", dataset_images_location, numFrame); //filename recebe o endereço do frame 2 até o frame 2000
    // repete-se o mesmo processo anterior
    Mat currImage_c = cv::imread(filename); 
    if (currImage_c.empty()) continue;
    cvtColor(currImage_c, currImage, cv::COLOR_BGR2GRAY);
    //cv::imwrite(filename, currImage);
    //cout<< "com cor: "<<currImage_c.size()<<" gray: "<<currImage.size()<<endl;
    vector<uchar> status;
    featureTracking(prevImage, currImage, prevFeatures, currFeatures, status);
    E = findEssentialMat(currFeatures, prevFeatures, focal, pp, cv::RANSAC, 0.999, 1.0, mask);
    //if ( ! E.isContinuous() ) continue; //Error terminate called after throwing an instance of 'cv::Exception' what():  OpenCV(4.5.5-dev) /home/matheus-ubuntu/opencv_build/opencv/modules/core/src/matrix.cpp:1175: error: (-13:Image step is wrong) The matrix is not continuous, thus its number of rows can not be changed in function 'reshape'
    
    recoverPose(E, currFeatures, prevFeatures, R, t, focal, pp, mask);
    // aqui ja muda. prevPts eh uma matrix 2 x prevFeatures do tipo 64-bits float 
    Mat prevPts(2, prevFeatures.size(), CV_64F), currPts(2, currFeatures.size(), CV_64F);

    for (int i = 0; i < prevFeatures.size(); i++) {   //this (x,y) combination makes sense as observed from the source code of triangulatePoints on GitHub
      prevPts.at<double>(0, i) = prevFeatures.at(i).x;
      prevPts.at<double>(1, i) = prevFeatures.at(i).y;

      currPts.at<double>(0, i) = currFeatures.at(i).x;
      currPts.at<double>(1, i) = currFeatures.at(i).y;
    }

    //This is cheating because ideally you'd want to figure out a way to get scale, but without this cheat there is a lot of drift.
    scale = groundScales[numFrame];

    //only update the current R and t if it makes sense.
    //if ((scale > 0.1) && (t.at<double>(2) > t.at<double>(0)) && (t.at<double>(2) > t.at<double>(1))) {
    scale =1;
      t_f = t_f + (R_f * t);
      R_f = R * R_f;

    //}

    //Make sure we have enough features to track
    if (prevFeatures.size() < MIN_NUM_FEAT) {
      featureDetection(prevImage, prevFeatures);
      featureTracking(prevImage, currImage, prevFeatures, currFeatures, status);
    }

    prevImage = currImage.clone();
    prevFeatures = currFeatures;

    int x = int(t_f.at<double>(0)) + 500; //coordenadas do ponto inicial x e y na visualização
    int y = int(t_f.at<double>(2)) +200;
    //cout<<"x: "<<x<<" e y: "<<y<<endl;
    //desenha um circulo em uma imagem. x e y são as coordenadas do centro do circulo. 1 eh o raio do circulo
    //cor vermelha, com thickness 2
    circle(traj, cv::Point(x, y), 1, CV_RGB(255, 0, 0), 2); // traj eh a matriz de zeros que vai ser recebido o desenho
    //em azul, desenhamos o ground truth
    //circle(traj, cv::Point(groundPoses[numFrame].x+200, groundPoses[numFrame].y+100), 1, CV_RGB(0, 0, 255), 2);

    //aqui expressamos as coordendas da translação em x, y e z
    rectangle(traj, cv::Point(10, 30), cv::Point(550, 50), CV_RGB(0, 0, 0), cv::FILLED);
    sprintf(text, "Coordinates: x = %02fm y = %02fm z = %02fm", t_f.at<double>(0), t_f.at<double>(1),t_f.at<double>(2));
    putText(traj, text, textOrg, fontFace, fontScale, cv::Scalar::all(255), thickness, 8);

    if (renderFeatures){
      //Draw features as markers for fun
      for(auto point: currFeatures)
        cv::drawMarker(currImage_c, cv::Point(point.x,point.y), CV_RGB(0,255,0), cv::MARKER_TILTED_CROSS, 2, 1, cv::LINE_AA);
    }

    Mat concated;
    //cout<<"currImage_c: "<<currImage_c.size()<<endl;
    //cout<<"traj: "<<traj.size()<<endl;
    //cout<<"concated: "<<currImage_c.size()<<endl;
    //cout<< "com cor: "<<currImage_c.size()<<" gray: "<<currImage.size()<<endl;
    cv::vconcat(currImage_c, traj, concated); //concatena verticalmente as matrizes currImage_c e traj. Obs.: o numero de colunas 
    //precisa ser o mesmo
    imshow("Road facing camera | Top Down Trajectory", currImage_c);
    if(numFrame > 83) imwrite("imagem.jpg", currImage_c);
    imshow("Road facing camera  Top Down Trajectory", traj);
    cv::waitKey(1);
  }

  clock_t end = clock();
  double elapsed_secs = double(end - begin) / CLOCKS_PER_SEC;
  cout << "Total time taken: " << elapsed_secs << "s" << endl;

  //let the user press a key to exit.
  cv::waitKey(0);

  return 0;
}