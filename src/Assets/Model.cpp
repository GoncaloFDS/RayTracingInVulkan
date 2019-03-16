#include "Model.hpp"
#include "Icosphere.hpp"
#include "Procedural.hpp"
#include "Sphere.hpp"
//#include "TextureImage.hpp"
#include "Utilities/Exception.hpp"
#include "Utilities/Console.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/hash.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#include <chrono>
#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace std
{
	template<> struct hash<Assets::Vertex> final
	{
		size_t operator()(Assets::Vertex const& vertex) const noexcept
		{
			return
				Combine(hash<glm::vec3>()(vertex.Position),
					Combine(hash<glm::vec3>()(vertex.Normal),
						Combine(hash<glm::vec2>()(vertex.TexCoord),
							hash<int>()(vertex.MaterialIndex))));
		}

	private:

		static size_t Combine(size_t hash0, size_t hash1)
		{
			return hash0 ^ hash1 + 0x9e3779b9 + (hash0 << 6) + (hash0 >> 2);
		}
	};
}

namespace Assets {

Model Model::LoadModel(const std::string& filename)
{
	std::cout << "Loading '" << filename << "'... " << std::flush;

	const auto timer = std::chrono::high_resolution_clock::now();
	const std::string materialPath = std::filesystem::path(filename).parent_path().string();
	
	tinyobj::attrib_t objAttrib;
	std::vector<tinyobj::shape_t> objShapes;
	std::vector<tinyobj::material_t> objMaterials;
	std::string warn;
	std::string err;

	if (!tinyobj::LoadObj(
		&objAttrib, &objShapes, &objMaterials, &warn, &err,
		filename.c_str(),
		materialPath.c_str()))
	{
		Throw(std::runtime_error("failed to load model '" + filename + "':\n" + err));
	}

	if (!warn.empty())
	{
		Utilities::Console::Write(Utilities::Severity::Warning, [&warn]()
		{
			std::cout << "\nWARNING: " << warn << std::flush;
		});
	}

	// Materials
	std::vector<Material> materials;

	for (const auto& material : objMaterials)
	{
		Material m{};

		m.Diffuse = glm::vec4(material.diffuse[0], material.diffuse[1], material.diffuse[2], 1.0);

		materials.emplace_back(m);
	}

	if (materials.empty())
	{
		Material m{};

		m.Diffuse = glm::vec4(0.7f, 0.7f, 0.7f, 1.0);

		materials.emplace_back(m);
	}

	// Geometry
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::unordered_map<Vertex, uint32_t> uniqueVertices(objAttrib.vertices.size());
	size_t faceId = 0;

	for (const auto& shape : objShapes)
	{
		const auto& mesh = shape.mesh;

		for (const auto& index : mesh.indices)
		{
			Vertex vertex = {};

			vertex.Position =
			{
				objAttrib.vertices[3 * index.vertex_index + 0],
				objAttrib.vertices[3 * index.vertex_index + 1],
				objAttrib.vertices[3 * index.vertex_index + 2],
			};

			vertex.Normal =
			{
				objAttrib.normals[3 * index.normal_index + 0],
				objAttrib.normals[3 * index.normal_index + 1],
				objAttrib.normals[3 * index.normal_index + 2]
			};

			if (!objAttrib.texcoords.empty())
			{
				vertex.TexCoord =
				{
					objAttrib.texcoords[2 * index.texcoord_index + 0],
					1 - objAttrib.texcoords[2 * index.texcoord_index + 1]
				};
			}

			vertex.MaterialIndex = std::max(0, mesh.material_ids[faceId++ / 3]);

			if (uniqueVertices.count(vertex) == 0)
			{
				uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
				vertices.push_back(vertex);
			}

			indices.push_back(uniqueVertices[vertex]);
		}
	}

	const auto elapsed = std::chrono::duration<float, std::chrono::seconds::period>(std::chrono::high_resolution_clock::now() - timer).count();

	std::cout << "(" << objAttrib.vertices.size() << " vertices, " << uniqueVertices.size() << " unique vertices, " << materials.size() << " materials) ";
	std::cout << elapsed << "s" << std::endl;

	return Model(std::move(vertices), std::move(indices), std::move(materials), nullptr);
}

Model Model::CreateSphere(const glm::vec3& center, float radius, int subdivision, const Material& material, const bool isProcedural)
{
	const Icosphere icosphere(radius, subdivision, true);

	std::vector<Vertex> vertices;
	vertices.reserve(icosphere.VertexCount());

	for (size_t i = 0; i != icosphere.VertexCount(); ++i)
	{
		Vertex v{};

		v.Position = glm::vec3(icosphere.Vertices()[i * 3 + 0], icosphere.Vertices()[i * 3 + 1], icosphere.Vertices()[i * 3 + 2]) + center;
		v.Normal = glm::vec3(icosphere.Normals()[i * 3 + 0], icosphere.Normals()[i * 3 + 1], icosphere.Normals()[i * 3 + 2]);
		v.TexCoord = glm::vec2(icosphere.TexCoords()[i * 2 + 0], icosphere.TexCoords()[i * 2 + 1]);
		v.MaterialIndex = 0;

		vertices.push_back(v);
	}

	return Model(
		std::move(vertices), 
		std::vector<uint32_t>(icosphere.Indices()), 
		std::vector<Material>{material},
		isProcedural ? new Sphere(center, radius) : nullptr);
}

void Model::SetMaterial(const Material& material)
{
	if (materials_.size() != 1)
	{
		Throw(std::runtime_error("cannot change material on a multi-material model"));
	}

	materials_[0] = material;
}

void Model::Transform(const glm::mat4& transform)
{
	const auto transformIT = glm::inverseTranspose(transform);

	for (auto& vertex : vertices_)
	{
		vertex.Position = transform * glm::vec4(vertex.Position, 1);
		vertex.Normal = transformIT * glm::vec4(vertex.Normal, 0);
	}
}

Model::Model(std::vector<Vertex>&& vertices, std::vector<uint32_t>&& indices, std::vector<Material>&& materials, const class Procedural* procedural) :
	vertices_(std::move(vertices)), 
	indices_(std::move(indices)),
	materials_(std::move(materials)),
	procedural_(procedural)
{
}

}