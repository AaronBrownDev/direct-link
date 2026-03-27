#----------
# CLUSTER
#----------
resource "google_container_cluster" "primary" {
  name     = var.cluster_name
  location = var.zone
  project  = var.project_id

  # Remove default node pool so we can manage separately.
  # Standard Terraform pattern that gives full control over
  # node pool lifecycle without risking cluster recreation.
  remove_default_node_pool = true
  initial_node_count       = 1

  # Disable for dev to reduce cost and complexity
  deletion_protection = false

  networking_mode = "VPC_NATIVE"
  network         = var.network

  ip_allocation_policy {
    # Let GKE handle this
  }

  workload_identity_config {
    workload_pool = "${var.project_id}.svc.id.goog"
  }
}

#-----------
# NODE POOL
#-----------
resource "google_container_node_pool" "primary" {
  name     = "${var.cluster_name}-pool"
  location = var.zone
  project  = var.project_id
  cluster  = google_container_cluster.primary.name

  node_count = var.node_count

  node_config {
    machine_type = var.machine_type
    spot         = var.spot
    disk_size_gb = var.disk_size_gb

    oauth_scopes = [
      "https://www.googleapis.com/auth/cloud-platform"
    ]

    labels = {
      environment = var.environment
    }

    tags = [var.cluster_name]

    workload_metadata_config {
      mode = "GKE_METADATA"
    }
  }

  management {
    auto_repair  = true
    auto_upgrade = true
  }
}

#-------------------
# FIREWALL RULES
#-------------------
resource "google_compute_firewall" "allow_signaling" {
  name    = "${var.cluster_name}-allow-signaling"
  network = var.network
  project = var.project_id

  allow {
    protocol = "tcp"
    ports    = ["50051"]
  }

  source_ranges = ["0.0.0.0/0"]
  target_tags   = [var.cluster_name]
}

resource "google_compute_firewall" "allow_livekit" {
  name    = "${var.cluster_name}-allow-livekit"
  network = var.network
  project = var.project_id

  allow {
    protocol = "tcp"
    ports    = ["7880", "7881"]
  }

  source_ranges = ["0.0.0.0/0"]
  target_tags   = [var.cluster_name]
}

resource "google_compute_firewall" "allow_whip" {
  name    = "${var.cluster_name}-allow-whip"
  network = var.network
  project = var.project_id

  allow {
    protocol = "tcp"
    ports    = ["8080"]
  }

  source_ranges = ["0.0.0.0/0"]
  target_tags   = [var.cluster_name]
}

resource "google_compute_firewall" "allow_webrtc_udp" {
  name    = "${var.cluster_name}-allow-webrtc-udp"
  network = var.network
  project = var.project_id

  allow {
    protocol = "udp"
    ports    = ["50000-60000"]
  }

  source_ranges = ["0.0.0.0/0"]
  target_tags   = [var.cluster_name]
}
