#include "TinyRenderer.h"

#include <vector>
#include <limits>
#include <iostream>
#include "tgaimage.h"
#include "model.h"
#include "geometry.h"
#include "our_gl.h"
#include "../Utils/b3ResourcePath.h"
#include "Bullet3Common/b3MinMax.h"
#include "../OpenGLWindow/ShapeData.h"
#include "LinearMath/btAlignedObjectArray.h"
#include "LinearMath/btVector3.h"

Matrix viewportmat;
float depth = 2.0;
int indexmap[10000000];
int count = 0;
bool setindex = true;

struct Shader : public IShader {
    
    Model* m_model;
    Vec3f m_light_dir_local;
    Vec3f m_light_color;
    Matrix& m_modelMat;
	Matrix m_invModelMat;

    Matrix& m_modelView1;
    Matrix& m_projectionMatrix;
    Vec3f m_localScaling;
    Vec4f m_colorRGBA;

    mat<2,3,float> varying_uv;  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    mat<4,3,float> varying_tri; // triangle coordinates (clip coordinates), written by VS, read by FS
    mat<3,3,float> varying_nrm; // normal per vertex to be interpolated by FS
    //mat<3,3,float> ndc_tri;     // triangle in normalized device coordinates

    Shader(Model* model, Vec3f light_dir_local, Vec3f light_color, Matrix& modelView, Matrix& projectionMatrix, Matrix& modelMat, Vec3f localScaling, const Vec4f& colorRGBA)
    :m_model(model),
    m_light_dir_local(light_dir_local),
    m_light_color(light_color),
    m_modelView1(modelView),
    m_projectionMatrix(projectionMatrix),
    m_modelMat(modelMat),
	m_localScaling(localScaling),
    m_colorRGBA(colorRGBA)
    {
        m_invModelMat = m_modelMat.invert_transpose();
    }

    virtual Vec4f vertex(int iface, int nthvert) {
        Vec2f uv = m_model->uv(iface, nthvert);
        varying_uv.set_col(nthvert, uv);
        //varying_nrm.set_col(nthvert, proj<3>((m_projectionMatrix*m_modelView).invert_transpose()*embed<4>(m_model->normal(iface, nthvert), 0.f)));
        varying_nrm.set_col(nthvert, proj<3>(m_invModelMat*embed<4>(m_model->normal(iface, nthvert), 0.f)));
        //m_localNormal = m_model->normal(iface, nthvert);
        //varying_nrm.set_col(nthvert, m_model->normal(iface, nthvert));
        Vec3f unScaledVert = m_model->vert(iface, nthvert);

        Vec3f scaledVert=Vec3f(unScaledVert[0]*m_localScaling[0],
                               unScaledVert[1]*m_localScaling[1],
                               unScaledVert[2]*m_localScaling[2]);

        Vec4f gl_Vertex = m_projectionMatrix*m_modelView1*embed<4>(scaledVert);
        varying_tri.set_col(nthvert, gl_Vertex);
        return gl_Vertex;
    }

    virtual bool fragment(Vec3f bar, TGAColor &color) {
        Vec3f bn = (varying_nrm*bar).normalize();
        Vec2f uv = varying_uv*bar;

        Vec3f reflection_direction = (bn * (bn * m_light_dir_local * 2.f) - m_light_dir_local).normalize();
        float specular = pow(b3Max(reflection_direction.z, 0.f), m_model->specular(uv));
        float diffuse = b3Max(0.f, bn * m_light_dir_local);

	float ambient_coefficient = 0.6;
        float diffuse_coefficient = 0.35;
        float specular_coefficient = 0.05;

        float intensity = ambient_coefficient + b3Min(diffuse * diffuse_coefficient + specular * specular_coefficient, 1.0f - ambient_coefficient);

        color = m_model->diffuse(uv) * intensity;

        //warning: bgra color is swapped to rgba to upload texture
        color.bgra[0] *= m_colorRGBA[0];
        color.bgra[1] *= m_colorRGBA[1];
        color.bgra[2] *= m_colorRGBA[2];
        color.bgra[3] *= m_colorRGBA[3];
        
        color.bgra[0] *= m_light_color[0];
        color.bgra[1] *= m_light_color[1];
        color.bgra[2] *= m_light_color[2];

        return false;
    }
};

struct DepthShader : public IShader {
    
    Model* m_model;
    Matrix& m_modelMat;
    Matrix m_invModelMat;
    
    Matrix& m_projectionMatrix;
    Vec3f m_localScaling;
    Matrix& m_lightModelView;
    
    mat<2,3,float> varying_uv;  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    mat<4,3,float> varying_tri; // triangle coordinates (clip coordinates), written by VS, read by FS
    
    mat<3,3,float> varying_nrm; // normal per vertex to be interpolated by FS
    
    DepthShader(Model* model, Matrix& lightModelView, Matrix& projectionMatrix, Matrix& modelMat, Vec3f localScaling)
    :m_model(model),
    m_lightModelView(lightModelView),
    m_projectionMatrix(projectionMatrix),
    m_modelMat(modelMat),
    m_localScaling(localScaling)
    {
        m_invModelMat = m_modelMat.invert_transpose();
    }
    virtual Vec4f vertex(int iface, int nthvert) {
        Vec2f uv = m_model->uv(iface, nthvert);
        varying_uv.set_col(nthvert, uv);
        varying_nrm.set_col(nthvert, proj<3>(m_invModelMat*embed<4>(m_model->normal(iface, nthvert), 0.f)));
        Vec3f unScaledVert = m_model->vert(iface, nthvert);
        Vec3f scaledVert=Vec3f(unScaledVert[0]*m_localScaling[0],
                               unScaledVert[1]*m_localScaling[1],
                               unScaledVert[2]*m_localScaling[2]);
        Vec4f gl_Vertex = m_projectionMatrix*m_lightModelView*embed<4>(scaledVert);
        varying_tri.set_col(nthvert, gl_Vertex);
        return gl_Vertex;
    }
    
    virtual bool fragment(Vec3f bar, TGAColor &color) {
        Vec4f p = varying_tri*bar;
        color = TGAColor(255, 255, 255)*(p[2]/depth);
        return false;
    }
};

struct ShadowShader : public IShader {
    
    Model* m_model;
    Vec3f m_light_dir_local;
    Vec3f m_light_color;
    Matrix& m_modelMat;
    Matrix m_invModelMat;
    
    Matrix& m_modelView1;
    Matrix& m_projectionMatrix;
    Vec3f m_localScaling;
    Matrix& m_lightModelView;
    Vec4f m_colorRGBA;
    
    b3AlignedObjectArray<float>& m_shadowBuffer;
    
    int m_width;
    int m_height;
    
    int m_index;
    
    mat<2,3,float> varying_uv;  // triangle uv coordinates, written by the vertex shader, read by the fragment shader
    mat<4,3,float> varying_tri; // triangle coordinates (clip coordinates), written by VS, read by FS
    mat<4,3,float> varying_tri_light_view;
    mat<3,3,float> varying_nrm; // normal per vertex to be interpolated by FS
    
    ShadowShader(Model* model, Vec3f light_dir_local, Vec3f light_color, Matrix& modelView, Matrix& lightModelView, Matrix& projectionMatrix, Matrix& modelMat, Vec3f localScaling, const Vec4f& colorRGBA, int width, int height, b3AlignedObjectArray<float>& shadowBuffer)
    :m_model(model),
    m_light_dir_local(light_dir_local),
    m_light_color(light_color),
    m_modelView1(modelView),
    m_lightModelView(lightModelView),
    m_projectionMatrix(projectionMatrix),
    m_modelMat(modelMat),
    m_localScaling(localScaling),
    m_colorRGBA(colorRGBA),
    m_width(width),
    m_height(height),
    m_shadowBuffer(shadowBuffer)
    {
        m_invModelMat = m_modelMat.invert_transpose();
    }
    virtual Vec4f vertex(int iface, int nthvert) {
        Vec2f uv = m_model->uv(iface, nthvert);
        varying_uv.set_col(nthvert, uv);
        varying_nrm.set_col(nthvert, proj<3>(m_invModelMat*embed<4>(m_model->normal(iface, nthvert), 0.f)));
        Vec3f unScaledVert = m_model->vert(iface, nthvert);
        Vec3f scaledVert=Vec3f(unScaledVert[0]*m_localScaling[0],
                               unScaledVert[1]*m_localScaling[1],
                               unScaledVert[2]*m_localScaling[2]);
        Vec4f gl_Vertex = m_projectionMatrix*m_modelView1*embed<4>(scaledVert);
        varying_tri.set_col(nthvert, gl_Vertex);
        Vec4f gl_VertexLightView = m_projectionMatrix*m_lightModelView*embed<4>(scaledVert);
        varying_tri_light_view.set_col(nthvert, gl_VertexLightView);
        return gl_Vertex;
    }
    
    virtual bool fragment(Vec3f bar, TGAColor &color) {
        Vec4f p = viewportmat*(varying_tri_light_view*bar);
        float depth = p[2];
        //p = p/p[3];
        //int index_x = b3Min(m_width-1, int(p[0]));
        //index_x = b3Max(0, index_x);
        //int index_y = b3Min(m_height-1, int(p[1]));
        //index_y = b3Max(0, index_y);
        //int idx = index_x + index_y*m_width; // index in the shadowbuffer array
        int idx = indexmap[count];
        float shadow = 0.8+0.2*(m_shadowBuffer[idx]<-depth+0.43); // magic coeff to avoid z-fighting
        
        //printf("count: %d, idx: %d\n", count, idx);
        //printf("shadowbuffer: %f, depth: %f\n", m_shadowBuffer[idx], -depth+0.43);
        
        Vec3f bn = (varying_nrm*bar).normalize();
        Vec2f uv = varying_uv*bar;
        
        Vec3f reflection_direction = (bn * (bn * m_light_dir_local * 2.f) - m_light_dir_local).normalize();
        float specular = pow(b3Max(reflection_direction.z, 0.f), m_model->specular(uv));
        float diffuse = b3Max(0.f, bn * m_light_dir_local);
        
        float ambient_coefficient = 0.6;
        float diffuse_coefficient = 0.35;
        float specular_coefficient = 0.05;
        
        float intensity = ambient_coefficient + b3Min(diffuse * diffuse_coefficient + specular * specular_coefficient, 1.0f - ambient_coefficient);
        
        color = m_model->diffuse(uv) * intensity * shadow;
        
        //warning: bgra color is swapped to rgba to upload texture
        color.bgra[0] *= m_colorRGBA[0];
        color.bgra[1] *= m_colorRGBA[1];
        color.bgra[2] *= m_colorRGBA[2];
        color.bgra[3] *= m_colorRGBA[3];
        
        color.bgra[0] *= m_light_color[0];
        color.bgra[1] *= m_light_color[1];
        color.bgra[2] *= m_light_color[2];
        
        return false;

    }
};

TinyRenderObjectData::TinyRenderObjectData(TGAImage& rgbColorBuffer,b3AlignedObjectArray<float>&depthBuffer,b3AlignedObjectArray<float>&shadowBuffer)
:m_rgbColorBuffer(rgbColorBuffer),
m_depthBuffer(depthBuffer),
m_shadowBuffer(shadowBuffer),
m_segmentationMaskBufferPtr(0),
m_model(0),
m_userData(0),
m_userIndex(-1),
m_objectIndex(-1)
{
    Vec3f       eye(1,1,3);
    Vec3f    center(0,0,0);
    Vec3f        up(0,0,1);
    m_lightDirWorld.setValue(0,0,0);
	m_localScaling.setValue(1,1,1);
    m_modelMatrix = Matrix::identity();
 
}



TinyRenderObjectData::TinyRenderObjectData(TGAImage& rgbColorBuffer,b3AlignedObjectArray<float>&depthBuffer, b3AlignedObjectArray<float>&shadowBuffer, b3AlignedObjectArray<int>* segmentationMaskBuffer, int objectIndex)
:m_rgbColorBuffer(rgbColorBuffer),
m_depthBuffer(depthBuffer),
m_shadowBuffer(shadowBuffer),
m_segmentationMaskBufferPtr(segmentationMaskBuffer),
m_model(0),
m_userData(0),
m_userIndex(-1),
m_objectIndex(objectIndex)
{
    Vec3f       eye(1,1,3);
    Vec3f    center(0,0,0);
    Vec3f        up(0,0,1);
    m_lightDirWorld.setValue(0,0,0);
	m_localScaling.setValue(1,1,1);
    m_modelMatrix = Matrix::identity();
 
}

void TinyRenderObjectData::loadModel(const char* fileName)
{
 //todo(erwincoumans) move the file loading out of here
   char relativeFileName[1024];
    if (!b3ResourcePath::findResourcePath(fileName, relativeFileName, 1024))
    {
        printf("Cannot find file %s\n", fileName);
    } else
    {
        m_model = new Model(relativeFileName);
    }   
}


void TinyRenderObjectData::registerMeshShape(const float* vertices, int numVertices,const int* indices, int numIndices, const float rgbaColor[4],
	unsigned char* textureImage, int textureWidth, int textureHeight)
{
	if (0==m_model)
    {
        m_model = new Model();
        m_model->setColorRGBA(rgbaColor);
		if (textureImage)
		{
			m_model->setDiffuseTextureFromData(textureImage,textureWidth,textureHeight);
		} else
		{
			/*char relativeFileName[1024];
			if (b3ResourcePath::findResourcePath("floor_diffuse.tga", relativeFileName, 1024))
			{
				m_model->loadDiffuseTexture(relativeFileName);
			}
             */
		}
		
		m_model->reserveMemory(numVertices,numIndices);
        for (int i=0;i<numVertices;i++)
        {
            m_model->addVertex(vertices[i*9],
                         vertices[i*9+1],
                         vertices[i*9+2],
                         vertices[i*9+4],
                         vertices[i*9+5],
                         vertices[i*9+6],
                         vertices[i*9+7],
                         vertices[i*9+8]);
        }
        for (int i=0;i<numIndices;i+=3)
        {
            m_model->addTriangle(indices[i],indices[i],indices[i],
                                 indices[i+1],indices[i+1],indices[i+1],
                                 indices[i+2],indices[i+2],indices[i+2]);
        }
    }
}

void TinyRenderObjectData::registerMesh2(btAlignedObjectArray<btVector3>& vertices, btAlignedObjectArray<btVector3>& normals,btAlignedObjectArray<int>& indices)
{
	if (0==m_model)
    {
		int numVertices = vertices.size();
		int numIndices = indices.size();

        m_model = new Model();
		char relativeFileName[1024];
		if (b3ResourcePath::findResourcePath("floor_diffuse.tga", relativeFileName, 1024))
		{
			m_model->loadDiffuseTexture(relativeFileName);
		}
		
        for (int i=0;i<numVertices;i++)
        {
            m_model->addVertex(vertices[i].x(),
                         vertices[i].y(),
                         vertices[i].z(),
                         normals[i].x(),
                         normals[i].y(),
                         normals[i].z(),
                         0.5,0.5);
        }
        for (int i=0;i<numIndices;i+=3)
        {
            m_model->addTriangle(indices[i],indices[i],indices[i],
                                 indices[i+1],indices[i+1],indices[i+1],
                                 indices[i+2],indices[i+2],indices[i+2]);
        }
    }
}

void TinyRenderObjectData::createCube(float halfExtentsX,float halfExtentsY,float halfExtentsZ)
{
    m_model = new Model();
    
    char relativeFileName[1024];
    if (b3ResourcePath::findResourcePath("floor_diffuse.tga", relativeFileName, 1024))
    {
        m_model->loadDiffuseTexture(relativeFileName);
    }
    

	int strideInBytes = 9*sizeof(float);
	int numVertices = sizeof(cube_vertices_textured)/strideInBytes;
	int numIndices = sizeof(cube_indices)/sizeof(int);

	for (int i=0;i<numVertices;i++)
	{
		m_model->addVertex(halfExtentsX*cube_vertices_textured[i*9],
                     halfExtentsY*cube_vertices_textured[i*9+1],
                     halfExtentsY*cube_vertices_textured[i*9+2],
                     cube_vertices_textured[i*9+4],
                     cube_vertices_textured[i*9+5],
                     cube_vertices_textured[i*9+6],
                     cube_vertices_textured[i*9+7],
                     cube_vertices_textured[i*9+8]);
	}
	for (int i=0;i<numIndices;i+=3)
    {
        m_model->addTriangle(cube_indices[i],cube_indices[i],cube_indices[i],
                             cube_indices[i+1],cube_indices[i+1],cube_indices[i+1],
                             cube_indices[i+2],cube_indices[i+2],cube_indices[i+2]);
    }
	
}

TinyRenderObjectData::~TinyRenderObjectData()
{
    delete m_model;
}

void TinyRenderer::renderObject(TinyRenderObjectData& renderData)
{
	int width = renderData.m_rgbColorBuffer.get_width();
	int height = renderData.m_rgbColorBuffer.get_height();

	Vec3f light_dir_local = Vec3f(renderData.m_lightDirWorld[0],renderData.m_lightDirWorld[1],renderData.m_lightDirWorld[2]);
    Vec3f light_color = Vec3f(renderData.m_lightColor[0],renderData.m_lightColor[1],renderData.m_lightColor[2]);
    Model* model = renderData.m_model;
    if (0==model)
        return;

	renderData.m_viewportMatrix = viewport(0,0,width, height);
	
    b3AlignedObjectArray<float>& zbuffer = renderData.m_depthBuffer;
    b3AlignedObjectArray<float>& shadowbuffer = renderData.m_shadowBuffer;
    int* segmentationMaskBufferPtr = (renderData.m_segmentationMaskBufferPtr && renderData.m_segmentationMaskBufferPtr->size())?&renderData.m_segmentationMaskBufferPtr->at(0):0;
    
    TGAImage tempFrame(width, height, TGAImage::RGB);
    TGAImage& frame = renderData.m_rgbColorBuffer;
    
    {
        Matrix lightViewMatrix = lookat(light_dir_local*depth, Vec3f(0.0,0.0,0.0), Vec3f(0.0,0.0,1.0));
        Matrix lightModelViewMatrix = lightViewMatrix*renderData.m_modelMatrix;
        Matrix lightViewProjectionMatrix = renderData.m_projectionMatrix;
        Matrix modelViewMatrix = renderData.m_viewMatrix*renderData.m_modelMatrix;
        viewportmat = renderData.m_viewportMatrix;
        Vec3f localScaling(renderData.m_localScaling[0],renderData.m_localScaling[1],renderData.m_localScaling[2]);
        
        
        DepthShader shader(model, lightModelViewMatrix, lightViewProjectionMatrix,renderData.m_modelMatrix, localScaling);
        
        setindex = true;
        count = 0;
        
		for (int i=0; i<model->nfaces(); i++) 
		{
            for (int j=0; j<3; j++) {
                shader.vertex(i, j);
            }
            triangle(shader.varying_tri, shader, tempFrame, &shadowbuffer[0], segmentationMaskBufferPtr, renderData.m_viewportMatrix, renderData.m_objectIndex);
            count++;
        }
        
        for (int k = 0; k < 76800; ++k)
        {
            if (shadowbuffer[k] > -1e30f+0.00001)
            {
                //printf("[%d]: %f\n", k, shadowbuffer[k]);
            }
        }
        
        ShadowShader shadowShader(model, light_dir_local, light_color, modelViewMatrix, lightModelViewMatrix, renderData.m_projectionMatrix,renderData.m_modelMatrix, localScaling, model->getColorRGBA(), width, height, shadowbuffer);
        
        setindex = false;
        count = 0;
        
        for (int i=0; i<model->nfaces(); i++)
        {
            for (int j=0; j<3; j++) {
                shadowShader.vertex(i, j);
            }
            triangle(shadowShader.varying_tri, shadowShader, frame, &zbuffer[0], segmentationMaskBufferPtr, renderData.m_viewportMatrix, renderData.m_objectIndex);
            count++;
        }
        
        
        /*
        Shader shader(model, light_dir_local, light_color, modelViewMatrix, renderData.m_projectionMatrix,renderData.m_modelMatrix, localScaling, model->getColorRGBA());
        
        //printf("Render %d triangles.\n",model->nfaces());
        for (int i=0; i<model->nfaces(); i++)
        {
            
            for (int j=0; j<3; j++) {
                shader.vertex(i, j);
            }
            triangle(shader.varying_tri, shader, frame, &zbuffer[0], segmentationMaskBufferPtr, renderData.m_viewportMatrix, renderData.m_objectIndex);
        }
        */
        
    }
        
}

void TinyRenderer::renderObjectDepth(TinyRenderObjectData& renderData)
{
    int width = renderData.m_rgbColorBuffer.get_width();
    int height = renderData.m_rgbColorBuffer.get_height();
    
    Vec3f light_dir_local = Vec3f(renderData.m_lightDirWorld[0],renderData.m_lightDirWorld[1],renderData.m_lightDirWorld[2]);
    Vec3f light_color = Vec3f(renderData.m_lightColor[0],renderData.m_lightColor[1],renderData.m_lightColor[2]);
    Model* model = renderData.m_model;
    if (0==model)
        return;
    
    renderData.m_viewportMatrix = viewport(0,0,width, height);
    
    b3AlignedObjectArray<float>& zbuffer = renderData.m_depthBuffer;
    b3AlignedObjectArray<float>& shadowbuffer = renderData.m_shadowBuffer;
    int* segmentationMaskBufferPtr = (renderData.m_segmentationMaskBufferPtr && renderData.m_segmentationMaskBufferPtr->size())?&renderData.m_segmentationMaskBufferPtr->at(0):0;
    
    TGAImage tempFrame(width, height, TGAImage::RGB);
    TGAImage& frame = renderData.m_rgbColorBuffer;
    
    {
        Matrix lightViewMatrix = lookat(light_dir_local*depth, Vec3f(0.0,0.0,0.0), Vec3f(0.0,0.0,1.0));
        Matrix lightModelViewMatrix = lightViewMatrix*renderData.m_modelMatrix;
        Matrix lightViewProjectionMatrix = renderData.m_projectionMatrix;
        Matrix modelViewMatrix = renderData.m_viewMatrix*renderData.m_modelMatrix;
        viewportmat = renderData.m_viewportMatrix;
        Vec3f localScaling(renderData.m_localScaling[0],renderData.m_localScaling[1],renderData.m_localScaling[2]);
        
        
        DepthShader shader(model, lightModelViewMatrix, lightViewProjectionMatrix,renderData.m_modelMatrix, localScaling);
        
        setindex = true;
        
        for (int i=0; i<model->nfaces(); i++)
        {
            for (int j=0; j<3; j++) {
                shader.vertex(i, j);
            }
            triangle(shader.varying_tri, shader, tempFrame, &shadowbuffer[0], segmentationMaskBufferPtr, renderData.m_viewportMatrix, renderData.m_objectIndex);
            count++;
        }
        
        for (int k = 0; k < 76800; ++k)
        {
            if (shadowbuffer[k] > -1e30f+0.00001)
            {
                //printf("[%d]: %f\n", k, shadowbuffer[k]);
            }
        }
        
    }
    
}

void TinyRenderer::renderObjectShadow(TinyRenderObjectData& renderData)
{
    int width = renderData.m_rgbColorBuffer.get_width();
    int height = renderData.m_rgbColorBuffer.get_height();
    
    Vec3f light_dir_local = Vec3f(renderData.m_lightDirWorld[0],renderData.m_lightDirWorld[1],renderData.m_lightDirWorld[2]);
    Vec3f light_color = Vec3f(renderData.m_lightColor[0],renderData.m_lightColor[1],renderData.m_lightColor[2]);
    Model* model = renderData.m_model;
    if (0==model)
        return;
    
    renderData.m_viewportMatrix = viewport(0,0,width, height);
    
    b3AlignedObjectArray<float>& zbuffer = renderData.m_depthBuffer;
    b3AlignedObjectArray<float>& shadowbuffer = renderData.m_shadowBuffer;
    int* segmentationMaskBufferPtr = (renderData.m_segmentationMaskBufferPtr && renderData.m_segmentationMaskBufferPtr->size())?&renderData.m_segmentationMaskBufferPtr->at(0):0;
    
    TGAImage tempFrame(width, height, TGAImage::RGB);
    TGAImage& frame = renderData.m_rgbColorBuffer;
    
    {
        Matrix lightViewMatrix = lookat(light_dir_local*depth, Vec3f(0.0,0.0,0.0), Vec3f(0.0,0.0,1.0));
        Matrix lightModelViewMatrix = lightViewMatrix*renderData.m_modelMatrix;
        Matrix lightViewProjectionMatrix = renderData.m_projectionMatrix;
        Matrix modelViewMatrix = renderData.m_viewMatrix*renderData.m_modelMatrix;
        viewportmat = renderData.m_viewportMatrix;
        Vec3f localScaling(renderData.m_localScaling[0],renderData.m_localScaling[1],renderData.m_localScaling[2]);
        
        ShadowShader shadowShader(model, light_dir_local, light_color, modelViewMatrix, lightModelViewMatrix, renderData.m_projectionMatrix,renderData.m_modelMatrix, localScaling, model->getColorRGBA(), width, height, shadowbuffer);
        
        setindex = false;
        
        for (int i=0; i<model->nfaces(); i++)
        {
            for (int j=0; j<3; j++) {
                shadowShader.vertex(i, j);
            }
            triangle(shadowShader.varying_tri, shadowShader, frame, &zbuffer[0], segmentationMaskBufferPtr, renderData.m_viewportMatrix, renderData.m_objectIndex);
            printf("count: %d\n", count);
            count++;
        }
    }
    
}
