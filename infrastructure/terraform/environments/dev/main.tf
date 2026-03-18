terraform {
  required_version = ">= 1.3"

  required_providers {
    google = {
      source  = "hashicorp/google"
      version = "~> 6.0"
    }
  }
}

provider "google" {
  project = var.project_id
  region  = var.region
}

module "gke_cluster" {
  source = "../../modules/gke-cluster"

  project_id   = var.project_id
  region       = var.region
  zone         = var.zone
  cluster_name = var.cluster_name
  machine_type = var.machine_type
  node_count   = var.node_count
  spot         = var.spot
  disk_size_gb = var.disk_size_gb
  network      = var.network
}
