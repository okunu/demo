//
// Created by okunu on 2023/4/2.
//

#ifndef DEMO_MODEL_M_H
#define DEMO_MODEL_M_H

#include "mesh_m.h"
#include "Shader_m.h"
#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

#include <opencv2/opencv.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

using namespace std;

unsigned int TextureFromFile(const char *path, const string &directory, bool gamma = false);

class Model {
public:
    vector<TextureInfo> textures_loaded;
    vector<Mesh> meshes;
    string directory;
    bool gammaCorrection;

    Model(){}

    Model(string const& path, bool gamma = false): gammaCorrection(gamma) {
        loadModel(path);
    }

    void Draw(Shader& shader) {
        for (unsigned int i = 0; i < meshes.size(); ++i) {
            meshes[i].Draw(shader);
        }
    }

private:
    void loadModel(string const& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate
            | aiProcess_GenSmoothNormals
            | aiProcess_FlipUVs
            | aiProcess_CalcTangentSpace);
        if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            LOGI("ERROR::ASSIMP:: %s", importer.GetErrorString());
            return;
        }
        // retrieve the directory path of the filepath
        directory = path.substr(0, path.find_last_of('/'));
        LOGI("directory:: %s", directory.c_str());
        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode *node, const aiScene *scene) {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }
        for (int i = 0; i < node->mNumChildren; ++i) {
            processNode(node->mChildren[i], scene);
        }
    }

    Mesh processMesh(aiMesh *mesh, const aiScene *scene) {
        vector<Vertex> vertices;
        vector<unsigned int> indices;
        vector<TextureInfo> textures;
        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            Vertex vertex;
            glm::vec3 vector;
            vector.x = mesh->mVertices[i].x;
            vector.y = mesh->mVertices[i].y;
            vector.z = mesh->mVertices[i].z;
            vertex.Position = vector;

            if (mesh->HasNormals())
            {
                vector.x = mesh->mNormals[i].x;
                vector.y = mesh->mNormals[i].y;
                vector.z = mesh->mNormals[i].z;
                vertex.Normal = vector;
            }

            if(mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
            {
                glm::vec2 vec;
                // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
                // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
                vec.x = mesh->mTextureCoords[0][i].x;
                vec.y = mesh->mTextureCoords[0][i].y;
                vertex.TexCoords = vec;
                // tangent
                vector.x = mesh->mTangents[i].x;
                vector.y = mesh->mTangents[i].y;
                vector.z = mesh->mTangents[i].z;
                vertex.Tangent = vector;
                // bitangent
                vector.x = mesh->mBitangents[i].x;
                vector.y = mesh->mBitangents[i].y;
                vector.z = mesh->mBitangents[i].z;
                vertex.Bitangent = vector;
            } else {
                vertex.TexCoords = glm::vec2(0.0f, 0.0f);
            }
            vertices.push_back(vertex);
        }

        // now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
        for(unsigned int i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            // retrieve all indices of the face and store them in the indices vector
            for(unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        vector<TextureInfo> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
        textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
        // 2. specular maps
        vector<TextureInfo> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
        textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
        // 3. normal maps
        std::vector<TextureInfo> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
        textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
        // 4. height maps
        std::vector<TextureInfo> heightMaps = loadMaterialTextures(material, aiTextureType_AMBIENT, "texture_height");
        textures.insert(textures.end(), heightMaps.begin(), heightMaps.end());

        // return a mesh object created from the extracted mesh data
        return Mesh(vertices, indices, textures);
    }

    vector<TextureInfo> loadMaterialTextures(aiMaterial *mat, aiTextureType type, string typeName) {
        vector<TextureInfo> textures;
        for (int i = 0; i < mat->GetTextureCount(type); ++i) {
            aiString str;
            mat->GetTexture(type, i, &str);
            bool skip = false;
            for (int j = 0; j < textures_loaded.size(); ++j) {
                if (std::strcmp(textures_loaded[j].path.data(), str.C_Str()) == 0) {
                    textures.push_back(textures_loaded[j]);
                    skip = true;
                    break;
                }
            }
            if (!skip) {
                TextureInfo textureInfo;
                textureInfo.id = TextureFromFile(str.C_Str(), directory);
                textureInfo.type = typeName;
                textureInfo.path = str.C_Str();
                textures.push_back(textureInfo);
                textures_loaded.push_back(textureInfo);
            }
        }
        return textures;
    }

    unsigned int TextureFromFile(const char* path, const string& directory, bool gamma = false) {
        string filename =string(path);
        filename = directory + '/' + filename;
        unsigned int textureID;
        glGenTextures(1, &textureID);

        LOGI("TextureFromFile path = %s", filename.c_str());
        cv::Mat textureImage = cv::imread(filename);
        if (!textureImage.empty()) {
            cv::cvtColor(textureImage, textureImage, CV_BGR2RGB);
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, textureImage.cols,
                         textureImage.rows, 0, GL_RGB, GL_UNSIGNED_BYTE, textureImage.data);
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        } else {
            LOGI("TextureFromFile cv read is empty");
        }
        return textureID;
    }
};



#endif //DEMO_MODEL_M_H
