output "cluster_name" {
  value = module.gke_cluster.cluster_name
}

output "cluster_endpoint" {
  value     = module.gke_cluster.cluster_endpoint
  sensitive = true
}

output "get_credentials_command" {
  value = module.gke_cluster.get_credentials_command
}

output "project_id" {
  value = module.gke_cluster.project_id
}
