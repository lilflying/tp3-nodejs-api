# DevOps TP — Group Project

Build · Containerize · Deploy · Monitor d'une application via la chaîne DevOps complète.

**Stack** : React (Vite) · Spring Boot (Java 21) · MySQL 8 · Docker · GitLab CI · Terraform · Ansible · Prometheus · Grafana

L'application est volontairement minimale (CRUD employés) : le cœur du TP est la **chaîne DevOps**, pas la richesse fonctionnelle.

---

## Mapping des livrables demandés

| Livrable attendu | Où c'est dans le repo |
|---|---|
| Application code | `frontend/` (React) + `backend/` (Spring Boot) |
| Docker setup | `backend/Dockerfile`, `frontend/Dockerfile`, `docker-compose.yml` |
| CI/CD pipeline | `.gitlab-ci.yml` |
| IaC scripts | `iac/terraform/` (provision VM) + `iac/ansible/` (déploiement) |
| Monitoring dashboard | `monitoring/` (Prometheus + Grafana, dashboard pré-câblé) |
| Live demo | suivre la section « Démarrage » ci-dessous |

---

## Architecture

```
┌──────────┐   /api    ┌──────────┐   JDBC    ┌────────┐
│ Frontend │ ───────►  │ Backend  │ ───────►  │ MySQL  │
│ (nginx)  │  proxy    │ (Spring) │           │        │
└──────────┘           └────┬─────┘           └────────┘
   :3000                    │ /actuator/prometheus
                            ▼
                     ┌────────────┐   scrape   ┌─────────┐
                     │ Prometheus │ ◄───────── │ Grafana │
                     │   :9090    │            │  :3001  │
                     └────────────┘            └─────────┘
```

---

## Démarrage (démo locale — 1 commande)

```bash
docker compose up -d --build
```

Puis ouvrir :

| Service | URL | Identifiants |
|---|---|---|
| Frontend | http://localhost:3000 | — |
| API (santé) | http://localhost:8080/actuator/health | — |
| API (métriques) | http://localhost:8080/actuator/prometheus | — |
| Prometheus | http://localhost:9090 | — |
| Grafana | http://localhost:3001 | admin / admin |

Dans Grafana, le dashboard **« Employees API — Monitoring »** est déjà provisionné (datasource Prometheus auto-configurée).

Arrêter : `docker compose down` (ajouter `-v` pour effacer les volumes).

---

## Déroulé de démo conseillé (≈ 8 min)

1. **Build & containerisation** — montrer les 2 Dockerfiles multi-stage, puis `docker compose up --build`.
2. **App qui tourne** — ouvrir le frontend, ajouter/supprimer un employé.
3. **CI/CD** — montrer l'onglet *CI/CD → Pipelines* sur GitLab : build backend (Gradle) + build frontend (Vite), puis job `deploy` qui tourne sur le runner self-hosted de la VM et lance `docker compose up -d --build` directement. Le deploy ne se déclenche que sur `main`.
4. **IaC** — montrer `terraform apply` (provisionne la VM) puis `ansible-playbook` (installe Docker + déploie). En local on saute l'exécution réelle, on explique le rôle.
5. **Monitoring** — générer du trafic (rafraîchir le front quelques fois), puis montrer le dashboard Grafana qui réagit en direct (req/s, latence, mémoire JVM, statut UP).

> Astuce : pour faire bouger les courbes pendant la démo :
> `for i in $(seq 1 50); do curl -s localhost:8080/api/employees > /dev/null; done`

---

## Déploiement sur VM (IaC complète)

### 1. Provisionner la VM — Terraform
```bash
cd iac/terraform
terraform init
terraform apply        # affiche l'IP publique en sortie
```
> Pas de compte cloud ? Réutiliser la VM Ubuntu du TP Ansible précédent et passer directement à l'étape 2 en mettant son IP dans l'inventaire.

### 2. Déployer l'app — Ansible
```bash
cd iac/ansible
# éditer inventory.ini avec l'IP de la VM
ansible-playbook -i inventory.ini deploy.yml
```
Le playbook installe Docker + Compose, copie le projet et lance `docker compose up -d --build` sur la VM.

---

## Structure

```
.
├── backend/              # API Spring Boot (CRUD + Actuator/Prometheus)
├── frontend/             # SPA React (Vite) servie par nginx
├── docker-compose.yml    # orchestration des 5 services
├── .gitlab-ci.yml        # GitLab CI/CD pipeline
├── iac/
│   ├── terraform/        # provisionnement de la VM
│   └── ansible/          # déploiement applicatif
└── monitoring/
    ├── prometheus/       # config de scraping
    └── grafana/          # datasource + dashboard provisionnés
```
