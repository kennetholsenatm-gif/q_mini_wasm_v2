# API Key Registration Guide

## Overview

This guide helps you register for API keys to unlock additional data sources for training your 100B parameter model. Many sources work WITHOUT keys (marked with ✓), but some require registration for higher rate limits or access.

---

## Tier 1: NO API KEY REQUIRED ✓

These sources work immediately with zero setup:

| Source | Data Type | Volume | URL |
|--------|-----------|--------|-----|
| **OpenAlex** | Academic papers | 250M+ works | api.openalex.org |
| **Gutendex** | Classic books | 76,000+ books | gutendex.com |
| **arXiv** | Physics/math/CS preprints | 2M+ papers | export.arxiv.org |
| **Wikidata** | Structured knowledge | 100M+ items | query.wikidata.org |
| **OEIS** | Integer sequences | 360K+ sequences | oeis.org |
| **USGS Earthquake** | Seismic data | Real-time | earthquake.usgs.gov |
| **SpaceX API** | Launch/rocket data | All launches | api.spacexdata.com |
| **Chronicling America** | Historic newspapers | 20M+ pages | chroniclingamerica.loc.gov |
| **GBIF** | Biodiversity | 2B+ records | api.gbif.org |

---

## Tier 2: Free API Keys (Recommended)

### 1. NASA API
- **URL**: https://api.nasa.gov
- **Registration**: 30 seconds, instant approval
- **Rate Limit**: 1,000 requests/hour with key (vs 30/hour without)
- **Use Case**: APOD, NeoWS, Earth imagery, Mars rover photos
- **Key Location**: `apis.nasa_key` in config

**How to Register:**
1. Visit https://api.nasa.gov
2. Fill in name and email
3. Key emailed instantly
4. Add to config: `nasa = "YOUR_KEY"`

---

### 2. GitHub API
- **URL**: https://github.com/settings/tokens
- **Registration**: Free GitHub account required
- **Rate Limit**: 5,000 requests/hour with key (vs 60/hour without)
- **Use Case**: Code repositories, SYCL docs, ML frameworks
- **Key Location**: `apis.github_key` in config

**How to Register:**
1. Go to GitHub Settings → Developer settings → Personal access tokens
2. Click "Generate new token (classic)"
3. Select scopes: `repo` (read access)
4. Copy token and add to config: `github = "ghp_xxxxxxxx"`

---

### 3. WolframAlpha API
- **URL**: https://developer.wolframalpha.com/portal/myapps/
- **Registration**: Free Wolfram account required
- **Rate Limit**: 2,000 requests/month free tier
- **Use Case**: Math computations, scientific data
- **Key Location**: `apis.wolfram_key` in config

**How to Register:**
1. Create account at https://developer.wolframalpha.com
2. Click "Get an AppID"
3. Select "WolframAlpha Short Answers API"
4. Copy AppID and add to config: `wolfram = "XXXXXX-XXXXXXXXXX"`

---

### 4. PubChem API
- **URL**: https://pubchem.ncbi.nlm.nih.gov/
- **Registration**: Optional (NCBI account recommended)
- **Rate Limit**: 5 requests/second without key, higher with key
- **Use Case**: Chemical structures, molecular data
- **Note**: Works without key but may be rate-limited

---

### 5. Protein Data Bank (PDB/RCSB)
- **URL**: https://www.rcsb.org/
- **Registration**: Not required for API access
- **Rate Limit**: No strict limits
- **Use Case**: 3D protein structures
- **Note**: Works immediately, no key needed

---

## Tier 3: Academic/Research APIs

### Semantic Scholar API
- **URL**: https://www.semanticscholar.org/product/api
- **Registration**: Instant with email
- **Rate Limit**: 100 requests/5 minutes free
- **Use Case**: Academic paper metadata, citations
- **Key Location**: `apis.semantic_scholar_key` in config

**How to Register:**
1. Visit https://www.semanticscholar.org/product/api
2. Click "Get API Key"
3. Enter email, key sent instantly
4. Add to config: `semantic_scholar = "YOUR_KEY"`

---

### CORE API
- **URL**: https://core.ac.uk/services/api/
- **Registration**: Academic email preferred
- **Rate Limit**: 10K requests/month free tier
- **Use Case**: Open access research papers
- **Key Location**: `apis.core_key` in config

---

## Configuration Template

Add to your `training_config.toml`:

```toml
[apis]
# Tier 2 - Free keys (register above)
wolfram = "YOUR_WOLFRAM_KEY"
github = "YOUR_GITHUB_TOKEN"
nasa = "YOUR_NASA_KEY"
semantic_scholar = "YOUR_SS_KEY"  # Optional

# Tier 1 - No key needed (leave as false or omit)
wolfram = false  # Uses demo mode
pubchem = true   # Works without key
arxiv = true     # No key needed
oeis = true      # No key needed
wikidata = true  # No key needed

# New no-key sources (always enabled)
openalex = true
gutendex = true
usgs_earthquake = true
spacex = true
chronicling_america = true
gbif = true
```

---

## Priority Recommendations

### For 100B Parameter Model Training:

**Must-Have (No Key):**
1. OpenAlex - Massive academic corpus
2. Gutendex - Classic literature
3. Chronicling America - Historical text
4. GBIF - Biodiversity data
5. arXiv - Technical papers

**Recommended (Free Key):**
1. NASA API - Earth/space science (1,000 req/hr)
2. GitHub API - Code repos (5,000 req/hr)
3. WolframAlpha - Math/science (2,000 req/month)

---

## Troubleshooting

### "Rate Limit Exceeded"
- Check if key is valid in config
- Wait for rate limit reset
- Some APIs have daily limits (Wolfram: 2,000/month)

### "Invalid API Key"
- Verify key is copied correctly (no extra spaces)
- Some keys expire - regenerate if needed
- Check key has correct permissions/scopes

### Sources Not Fetching
- Check console for error messages
- Verify API endpoint URLs are accessible
- Some APIs block by IP region (use VPN if needed)

---

## API Key Security

**NEVER:**
- Commit keys to git repositories
- Share keys in public forums
- Include keys in client-side code

**ALWAYS:**
- Store keys in local config files (gitignored)
- Use environment variables for CI/CD
- Rotate keys periodically

---

## Summary Table

| Source | Needs Key | Free Tier | Priority |
|--------|-----------|-----------|----------|
| OpenAlex | ✗ | Unlimited | 🔥 CRITICAL |
| Gutendex | ✗ | Unlimited | 🔥 CRITICAL |
| arXiv | ✗ | Unlimited | 🔥 CRITICAL |
| Wikidata | ✗ | Unlimited | 🔥 CRITICAL |
| GBIF | ✗ | 100K/hour | 🔥 CRITICAL |
| Chronicling America | ✗ | Unlimited | 🔥 CRITICAL |
| NASA API | ✓ | 1,000/hr | HIGH |
| GitHub API | ✓ | 5,000/hr | HIGH |
| WolframAlpha | ✓ | 2,000/mo | MEDIUM |
| PubChem | Optional | 5/sec | MEDIUM |
| Semantic Scholar | ✓ | 100/5min | LOW |

---

**Total Free Data Available:**
- No-Key Sources: ~300M+ documents
- With Free Keys: ~400M+ documents

**Registration Time:** ~5 minutes total
