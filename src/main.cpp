//****************************************************
// Starter code for assignment #1.  It is provided to 
// help get you started, but you are not obligated to
// use this starter code.
//****************************************************

#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/types.h>
#include <sys/stat.h>

//include header file for glfw library so that we can use OpenGL
#ifdef _WIN32
/* this has to be done because my system does not have FREETYPE, delete this line if platform is otherwise
STRONGLY NOT RECOMMENDED for GLFW setup */
#include <windows.h>
#endif
#include "fast_math.h" //include a faster version of some math ops we wrote
#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <time.h>
#include <ctime>

//logger-related imp
#include <chrono>
#include <stdarg.h>

//include assimp for model file imports
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

//pipelines
#include "geometry.h"     //geometry pipeline
#include "AABBTree.h"     //aabb tree pipeline to accelerate hit detection
#include "lights.h"       //illumination pipeline
#include "cameras.h"      //viewport pipeline
#include "rasterizer.h"   //rasterizer pipeline
#include "toojpeg.h"      //rasterizer img IO pipeline
//dirent for file and directories management
#include "dirent.h"

#ifdef _WIN32
static DWORD lastTime;
#else
static struct timeval lastTime;
#endif

//define the system path divider
#if defined(WIN32) || defined(_WIN32) 
#define PATH_SEPARATOR "\\" 
#else 
#define PATH_SEPARATOR "/"
#endif 

using namespace std;

//****************************************************
// Global Variables
// Generally speaking, global variables should be 
// avoided, but for this small assignment we'll make
// an exception.
//****************************************************

GLfloat Translation[3] = {0.0f, 0.0f, 0.0f};
bool Auto_strech = false;
int  Width_global = 960;
int  Height_global = 540;

int  SizeX_saved_global;
int  SizeY_saved_global;

int  PosX_saved_global;
int  PosY_saved_global;

//status
bool rendering = false;

//log file
FILE* fp = fopen("logs.txt", "w");

//parameters
bool suspended = false;

const float PI = 3.1415926;

string scene_file_dir = "defaultScene"; //the default scene directory to render

//scene objects
vector<Mesh*> meshes;
AABBTree* aabb_tree;
vector<Light*> lights;
vector<Camera*> cams;

//ray parameters
float set_hfov = 54.43; //horizontal fov of the camera, default to maya standard default 54.43
float epsilon = 0.0001f; //epsilon to add to next ray for avoiding self collision
float ray_eps = 0.1f; //epsilon to jitter the splitted ray, hight means more random and soft results
int samples_per_pixel = 2; //sample monte carlo rays per pixel width, defaulted to 2, actual sample number is its square
int samples_per_ray = 4; //sample rays per parent ray, defaulted to 4, actual number will be this number plus one
int ray_pool_page_size = 64; //page size for the ray allocator
int thread_n = ceil(fast_sqrt(thread::hardware_concurrency())); //concurrent thread numbers when rendering the scene (set to match cpu core amounts), actual thread num is this num squared and plus some
int max_ray_bounce = 3; //maximum bounce time of a ray
int max_refrac_bounce = 3; //maximum bounce time of a refractive ray

Rasterizer rasterizer = Rasterizer(Width_global, Height_global); //a single rasterizer pipeline (TODO: enable multi-pipeline rendering for quicker rerender)
//rasterizer parameters
int prog_disp_span = 100; //block of pixels to progressively display
std::ofstream out_file("render_result.jpg", std::ios_base::out | std::ios_base::binary); //out stream to save the rasterizer's image to

const GLFWvidmode * VideoMode_global = NULL;

//****************************************************
// Log Stuff
//****************************************************

//function to print to a log file (logs.txt) and stdout at the same time
void logprintf(char* format, ...)
{
    va_list ap;
    va_list ap2;

    va_start(ap, format);
    va_copy(ap2, ap);

    vfprintf(fp, format, ap);
    va_end(ap);

    vprintf(format, ap2);
    va_end(ap2);
}

//****************************************************
// Image IO Stuff
//****************************************************

void write_char(unsigned char byte) {
    out_file << byte;
}

bool save_jpg() {
    // image params
    const auto width = rasterizer.getWidth();
    const auto height = rasterizer.getHeight();
    // RGB: one byte each for red, green, blue
    const auto bytesPerPixel = 3;
    // allocate memory
    auto image = new unsigned char[width * height * bytesPerPixel];
    // create img
    for (auto y = 0; y < height; y++)
        for (auto x = 0; x < width; x++)
        {
            // memory location of current pixel
            auto offset = (y * width + x) * bytesPerPixel;
            // rgb
            vec3 c;
            rasterizer.getColor(x, height - y - 1, c);
            image[offset] = max(0, min(255, (int)floor(c[0] * 256.0)));
            image[offset + 1] = max(0, min(255, (int)floor(c[1] * 256.0)));
            image[offset + 2] = max(0, min(255, (int)floor(c[2] * 256.0)));
        }
    // start JPEG compression
    // note: myOutput is the function defined in line 18, it saves the output in example.jpg
    // optional parameters:
    const bool isRGB = true;  // true = RGB image, else false = grayscale
    const auto quality = 90;    // compression quality: 0 = worst, 100 = best, 80 to 90 are most often used
    const bool downsample = false; // false = save as YCbCr444 JPEG (better quality), true = YCbCr420 (smaller file)
    const char* comment = "Render Result"; // arbitrary JPEG comment
    auto ok = TooJpeg::writeJpeg(write_char, image, width, height, isRGB, quality, downsample, comment);
    delete[] image;
    // error => exit code 1
    return ok ? 1 : 0;
}

//****************************************************
// Simple init function
//****************************************************

void initializeRendering()
{
    glfwInit();
}

//****************************************************
// A routine to set a pixel by drawing a GL point.  This is not a
// general purpose routine as it assumes a lot of stuff specific to
// this example.
//****************************************************

void setPixel(float x, float y, GLfloat r, GLfloat g, GLfloat b) {
    glColor3f(r, g, b);
    glVertex2f(x+0.5, y+0.5);  
    // The 0.5 is to target pixel centers
    // Note that some OpenGL implementations have created gaps in the past.
}

//another function to draw a GL line given parameters
void setLine(float x, float y, float x1, float y1, GLfloat r, GLfloat g, GLfloat b){
    glBegin(GL_LINES);
    glColor3f(r, g, b);
    glVertex2f(x + 0.5f, y + 0.5f);
    glVertex2f(x1 + 0.5f, y1 + 0.5f);
    glEnd();
}

//****************************************************
// Draw a filled Frame with loaded scene
//****************************************************

//function to retrieve all files of a certian type from the directory provided
void retrieve_files(const string& pathname, vector<string>& filenames) {
    logprintf("\n[IO Report]\n");
    DIR* dir;
    struct dirent* ent;
    dir = opendir(pathname.c_str());
    if (dir != NULL) {
        /* print all the files within directory and save the file names to paths*/
        while ((ent = readdir(dir)) != NULL) {
            char* filename = ent->d_name;
            if (ent->d_type == DT_DIR) continue;
            logprintf("Detected %s\n", filename);
            filenames.push_back(filename);
        }
        closedir(dir);
    }
    else {
        /* could not open directory */
        logprintf("Directory corrupted.\n");
    }
}

//function to retrieve a node's global transform matrix in a scene
void retrieve_node_gtrans(aiMatrix4x4& out, const aiScene* scene, const char* node_name) {
    aiNode* this_node = scene->mRootNode->FindNode(node_name);
    //traverse through hierachy to get all transform matrices
    vector<aiMatrix4x4> trans_matrices;
    while (this_node->mParent != NULL) {
        trans_matrices.push_back(this_node->mTransformation);
        this_node = this_node->mParent;
    }
    //multiply the transform matrix
    while (!trans_matrices.empty()) {
        out *= trans_matrices[0];
        trans_matrices.erase(trans_matrices.begin());
    }
}

//function to load a scene
bool load_scene(const string& dir) {
    //Create an instance of the Importer class
    Assimp::Importer importer;

    //********************************
    // FILE SYSTEM STUFF GOES HERE
    //********************************

    //find all scene files
    vector<string> scene_paths;
    retrieve_files(dir, scene_paths);

    //import all scenes in the input folder
    vector<string> scene_names;
    for (int i = 0; i < scene_paths.size(); i++) {
        string filename = "";
        filename += dir;
        filename += PATH_SEPARATOR;
        filename += string(scene_paths[i]);

        scene_names.push_back(filename);
    }

    //prompt the user to choose scenes if there are multiples [TO-DO: Option of merge, though it's not actually related to the renderer]
    string scene_name;
    if (scene_names.size() >= 2) {
        logprintf("\nMultiple scenes detected, choose the scene to render by index:\n");
        for (int i = 0; i < scene_names.size(); i++) {
            logprintf("[%d] %s\n", i, scene_names[i].c_str());
        }
        int index = -1;
        while (index < 0 || index >= scene_names.size()) scanf("%d", &index);
        logprintf("Chosen Scene %s to render.\n", scene_names[index].c_str());
        scene_name = scene_names[index];
    }
    else scene_name = scene_names[0];

    //analyze and record the data we need
    //we will have tangent, triangles, vertices optimiaztion, forced explicit uv mapping, aabb bounding box after this
    const aiScene* scene = importer.ReadFile(scene_name,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals | //smooth the normals
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_GenUVCoords |
        aiProcess_SortByPType);

    //If the import failed, report it
    if (!scene) {
        logprintf("\n[Importing Error (ASSIMP)]\n%s\n", importer.GetErrorString());
        return false;
    }
    else logprintf("Loaded Scene %s\n", scene_name.c_str());

    //*******************************
    // LOADING STUFF GOES BELOW
    //*******************************

    //load meshes and create geometry classes from them
    logprintf("\nDetected Meshes:\n\n");
    for (int i = 0; i < scene->mNumMeshes; i++) {
        aiMesh* mesh = scene->mMeshes[i];
        //apply global trans
        aiMatrix4x4 gtrans;
        aiMatrix4x4 gtrans2;
        retrieve_node_gtrans(gtrans, scene, mesh->mName.C_Str());
        retrieve_node_gtrans(gtrans2, scene, mesh->mName.C_Str());
        gtrans2.Inverse().Transpose();
        //get the rotation/scaling part only
        for (int j = 0; j < mesh->mNumVertices; j++) {
            mesh->mVertices[j] = gtrans * mesh->mVertices[j];
            mesh->mNormals[j] =  gtrans2 * mesh->mNormals[j];
            mesh->mNormals[j].Normalize();
        }
        //get this meshes material
        aiMaterial* aimesh_mat = scene->mMaterials[mesh->mMaterialIndex];
        Material* mesh_mat;
        if (strstr(aimesh_mat->GetName().C_Str(), "lambert") != NULL) {
            mesh_mat = new LambertMat(aimesh_mat);
            logprintf("Associated this mesh with lambert material: %s\n", aimesh_mat->GetName().C_Str());
        }
        else if (strstr(aimesh_mat->GetName().C_Str(), "phong") != NULL) {
            mesh_mat = new PhongMat(aimesh_mat);
            logprintf("Associated this mesh with phong material: %s\n", aimesh_mat->GetName().C_Str());
        }
        else if (strstr(aimesh_mat->GetName().C_Str(), "refrac") != NULL) {
            mesh_mat = new RefracMat(aimesh_mat);
            logprintf("Associated this mesh with simple refractive material: %s\n", aimesh_mat->GetName().C_Str());
        }
        //if not material is specified, just assign a yellow hittest mat
        else {
            mesh_mat = new Material();
            logprintf("Can't find a proper material defined for this material, using default: %s\n", aimesh_mat->GetName().C_Str());
        }
        //store
        Mesh* new_mesh = new Mesh(mesh, mesh_mat);
        meshes.push_back(new_mesh);
        logprintf("%s\n", mesh->mName.C_Str()); //log the names of the meshes inside
    }

    //construct the aabb tree for optimized hit detection
    logprintf("\nConstructing AABB Tree for hit detection...");
    aabb_tree = new AABBTree(meshes);
    logprintf("done\n");

    //load scene again, but this time with pretransform so we get camera stuff easily
    const aiScene* cam_scene = importer.ReadFile(scene_name, aiProcess_PreTransformVertices);

    //load lights
    logprintf("\nDetected Lights:\n\n");
    for (int i = 0; i < cam_scene->mNumLights; i++) {
        aiLight* light = cam_scene->mLights[i];
        //reverse light direction for rendering purpose
        light->mDirection *= -1.0f;
        Light* new_light;
        if (light->mType == aiLightSourceType::aiLightSource_DIRECTIONAL) new_light = new DirectLight(light); //directional light
        else if (light->mType == aiLightSourceType::aiLightSource_POINT) new_light = new PointLight(light);   //point light
        lights.push_back(new_light);
        logprintf("%s\n", light->mName.C_Str());
        logprintf("Position: %.2f %.2f %.2f; Direction: %.2f %.2f %.2f; Color: %.2f %.2f %.2f;\n",
            light->mPosition.x, light->mPosition.y, light->mPosition.z,
            light->mDirection.x, light->mDirection.y, light->mDirection.z,
            light->mColorDiffuse.r, light->mColorDiffuse.g, light->mColorDiffuse.b);
    }

    //load cameras
    logprintf("\nDetected Cameras:\n\n");
    for (int i = 0; i < cam_scene->mNumCameras; i++) {
        aiCamera* cam = cam_scene->mCameras[i];
        aiMatrix4x4 cam_mat;
        cam->GetCameraMatrix(cam_mat);
        //store
        Camera* new_cam = new Camera(cam, cam_mat);
        cams.push_back(new_cam);
        logprintf("%s\n", cam->mName.C_Str());
    }

    // We're done. Everything will be cleaned up by the importer destructor
    return true;
}

//draw a rendered frame by looping over each pixel on the rasterizer
void drawFrame() {

    // Start drawing a list of points
    glBegin(GL_POINTS);

    //looping over the entire rasterizer buffer and draw pixels
    for (int i = 0; i < rasterizer.getWidth(); i++) {
        for (int j = 0; j < rasterizer.getHeight(); j++) {
            vec3 c;
            rasterizer.getColor(i, j, c);
            setPixel(i, j, c[0], c[1], c[2]);
        }
    }

    glEnd();
}

//**********************************************************
// function that does the actual drawing/rendering of stuff
//**********************************************************

void display(GLFWwindow* window)
{
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT); //clear buffer

    glMatrixMode(GL_MODELVIEW);                  // indicate we are specifying camera transformations
    glLoadIdentity();                            // make sure transformation is "zero'd"

    //----------------------- code to draw objects --------------------------
    glPushMatrix();
    glTranslatef(Translation[0], Translation[1], Translation[2]);

    drawFrame();

    glPopMatrix();

    glfwSwapBuffers(window);

}

//render the current frame and push data to rasterizer, render the realtime progress in the window specified
void renderFrame(GLFWwindow* window) {
    if (rendering) return;
    rendering = true;

    //***************************
    // FINAL RESULTS HANDLING
    //***************************

    //display final results
    display(window);
    rendering = false;
}

//****************************************************
// Keyboard inputs
//****************************************************

static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    switch (key) {
                                    
        case GLFW_KEY_ESCAPE: 
        case GLFW_KEY_Q:
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (rendering && suspended) {
                //save the jpg img
                if (save_jpg()) logprintf("Render result saved to render_result.jpg\n");
                else logprintf("IO Error: writing jpg image failed.\n");
                out_file.close();
                exit(0); //quit directly if suspended
            }
            break; 
        case GLFW_KEY_LEFT :
            Translation[0]-=1.0f;
            break;
        case GLFW_KEY_RIGHT:
            Translation[0]+=1.0f;
            break;
        case GLFW_KEY_UP   :
            Translation[1]+=1.0f;
            break;
        case GLFW_KEY_DOWN :
            Translation[1]-=1.0f;
            break;
        case GLFW_KEY_F:
            if (action) {
                Auto_strech = !Auto_strech;                 
                if (Auto_strech){
                    glfwGetWindowPos(window, &PosX_saved_global, &PosY_saved_global);
                    glfwGetWindowSize(window, &SizeX_saved_global, &SizeY_saved_global);
                    glfwSetWindowSize(window, VideoMode_global->width, VideoMode_global->height);
                    glfwSetWindowPos(window, 0, 0);
                }else{
                    glfwSetWindowSize(window, SizeX_saved_global, SizeY_saved_global);
                    glfwSetWindowPos(window, PosX_saved_global, PosY_saved_global);
                }
            }
            break;
        case GLFW_KEY_SPACE:
            if (action == GLFW_RELEASE) {
                //if rendering suspend/continue renderer
                if (rendering) {
                    suspended = !suspended;
                    if (suspended) logprintf("Rendering suspended by user.\n");
                    else logprintf("Rendering resumed by user.\n");
                }
                else renderFrame(window);
            }
            break;

        default: 
            break;
    }
}

//****************************************************
// function that is called when window is resized
//***************************************************

void size_callback(GLFWwindow* window, int width, int height)
{
    // The width and height arguments are not used
    // because they are not the size of the window 
    // in pixels.

    // Get the pixel coordinate of the window
    // it returns the size, in pixels, of the 
    // framebuffer of the specified window
    glfwGetFramebufferSize(window, &Width_global, &Height_global);
    
    glViewport(0, 0, Width_global, Height_global);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, Width_global, 0, Height_global, 1, -1);
    
    display(window);
}


//****************************************************
// the usual stuff, nothing exciting here
//****************************************************

enum class token_code {
    not_specified,
    set_display_width,
    set_display_height,
    set_horizontal_fov,
    set_spp,
    set_bounce,
    set_pixel_blk_size,
    set_samples_per_ray,
    set_epsilon,
    set_ray_eps,
    set_refrac_bounce
};

token_code get_token_code(string const& token){
    if (token == "-dispw") return token_code::set_display_width;
    if (token == "-disph") return token_code::set_display_height;
    if (token == "-hfov") return token_code::set_horizontal_fov;
    if (token == "-spp") return token_code::set_spp;
    if (token == "-bounce") return token_code::set_bounce;
    if (token == "-mpbs") return token_code::set_pixel_blk_size;
    if (token == "-spr") return token_code::set_samples_per_ray;
    if (token == "-eps") return token_code::set_epsilon;
    if (token == "-reps") return token_code::set_ray_eps;
    if (token == "-rbounce") return token_code::set_refrac_bounce;
    return token_code::not_specified;
}

void read_cmd_tokens(const vector<string> tokens){

    switch (get_token_code(tokens[0])) {
        case token_code::set_display_width:
            Width_global = stoi(tokens[1]);
            break;
        case token_code::set_display_height:
            Height_global = stoi(tokens[1]);
            break;
        case token_code::set_horizontal_fov:
            set_hfov = stof(tokens[1]);
            break;
        case token_code::set_spp:
            samples_per_pixel = stoi(tokens[1]);
            break;
        case token_code::set_bounce:
            max_ray_bounce = stoi(tokens[1]);
            break;
        case token_code::set_pixel_blk_size:
            prog_disp_span = stoi(tokens[1]);
            break;
        case token_code::set_samples_per_ray:
            samples_per_ray = stoi(tokens[1]);
            break;
        case token_code::set_epsilon:
            epsilon = stof(tokens[1]);
            break;
        case token_code::set_ray_eps:
            ray_eps = stof(tokens[1]);
            break;
        case token_code::set_refrac_bounce:
            max_refrac_bounce = stoi(tokens[1]);
            break;
        default:
            break;
    }

}

//method to check if a folder exists
//reference: https://stackoverflow.com/questions/18100097/portable-way-to-check-if-directory-exists-windows-linux-c
bool folder_exists(const char* pathname) {
    struct stat info;

    if (stat(pathname, &info) != 0)
        logprintf("Cannot access %s\n", pathname);
    else if (info.st_mode & S_IFDIR) { // S_ISDIR() doesn't exist on my windows 
        return true;
    }
    else
        logprintf("%s is no directory\n", pathname);
    return false;
}

int main(int argc, char *argv[]) {

    //take user input of scene file and options file
    char buffer[32];
    logprintf("Enter Scene Folder (max 32 chars): ");
    scanf(" %32s", buffer);
    logprintf("Initialized using scene folder [%s]\n", buffer);
    //check if directory exists
    if (!folder_exists(buffer)) {
        logprintf("Can't open the scene file, using default.\n");
    }
    else scene_file_dir = buffer;

    char buffer1[32];
    logprintf("Enter Option File (max 32 chars): ");
    scanf(" %32s", buffer1);
    logprintf("Initialized using option file [%s]\n", buffer1);

    //open option file and record variables
    ifstream input_stream(buffer1);
    if(!input_stream){
        logprintf("Can't open the option file, using default.\n");
    }
    
    //read lines for values
    vector<string> text;
    string line;
    while(getline(input_stream, line)) text.push_back(line);
    input_stream.close();
    for(int i=0; i<text.size();i++){
        string cmd = text[i];
        vector<string> split_cmds;
        stringstream ss(cmd);
        string token;
        while(getline(ss, token, ' ')) split_cmds.push_back(token);
        read_cmd_tokens(split_cmds);
    }

    //load scene
    load_scene(scene_file_dir);

    //This initializes glfw
    initializeRendering();
    
    GLFWwindow* window = glfwCreateWindow( Width_global, Height_global, "RayTracer", NULL, NULL );
    if ( !window )
    {
        cerr << "Error on window creating" << endl;
        glfwTerminate();
        return -1;
    }
    
    VideoMode_global = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if ( !VideoMode_global )
    {
        cerr << "Error on getting monitor" << endl;
        glfwTerminate();
        return -1;
    }
    
    glfwMakeContextCurrent( window );

    size_callback(window, 0, 0);
    
    glfwSetWindowSizeCallback(window, size_callback);
    glfwSetKeyCallback(window, key_callback);
                
    while( !glfwWindowShouldClose( window ) ) // main loop to draw object again and again
    {
        //display( window );

        glfwPollEvents();        
    }

    //save the jpg img
    if (save_jpg()) logprintf("Render result saved to render_result.jpg\n");
    else logprintf("IO Error: writing jpg image failed.\n");
    out_file.close();

    //close the log file
    fclose(fp);

    return 0;
}
