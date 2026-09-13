$ErrorActionPreference = "Stop"

$Repo = "Aakansha-Sharma1/InitraOS"
$ProjectNumber = 2
$ProjectOwner = "Aakansha-Sharma1"

$ProjectId = "PVT_kwHODRBbRc4BjSFv"
$StatusFieldId = "PVTSSF_lAHODRBbRc4BjSFvzhiHAfU"
$TodoOptionId = "f75ad846"

# ============================================================
# Milestones 12-23 and their issues
# ============================================================

$Milestones = @{

    12 = @{
        Title = "12 - Advanced Memory Management"
        Issues = @(
            "Implement kernel memory mapping"
            "Implement dynamic page allocation"
            "Implement heap and paging integration"
            "Implement memory protection flags"
            "Validate advanced memory management"
        )
    }

    13 = @{
        Title = "13 - Process & User-Space Architecture"
        Issues = @(
            "Define process architecture"
            "Implement process address spaces"
            "Implement user-space stack"
            "Implement user-space memory regions"
            "Implement process creation"
            "Validate process isolation"
        )
    }

    14 = @{
        Title = "14 - System Calls"
        Issues = @(
            "Define system call interface"
            "Implement system call entry"
            "Implement system call dispatcher"
            "Implement basic process system calls"
            "Implement basic memory system calls"
            "Validate system call interface"
        )
    }

    15 = @{
        Title = "15 - Executable & User Program Support"
        Issues = @(
            "Define user program format"
            "Implement executable loader"
            "Implement user program startup"
            "Implement program termination"
            "Validate user program execution"
        )
    }

    16 = @{
        Title = "16 - Filesystem"
        Issues = @(
            "Define filesystem architecture"
            "Implement block storage interface"
            "Implement filesystem initialization"
            "Implement file operations"
            "Implement directory operations"
            "Integrate filesystem with user programs"
            "Validate filesystem"
        )
    }

    17 = @{
        Title = "17 - Device & Hardware Abstraction"
        Issues = @(
            "Define device abstraction layer"
            "Implement device manager"
            "Implement storage device driver interface"
            "Implement keyboard device abstraction"
            "Implement timer device abstraction"
            "Validate hardware abstraction"
        )
    }

    18 = @{
        Title = "18 - Networking"
        Issues = @(
            "Define networking architecture"
            "Implement network device interface"
            "Implement Ethernet support"
            "Implement IP networking"
            "Implement basic network protocols"
            "Validate networking"
        )
    }

    19 = @{
        Title = "19 - User Environment"
        Issues = @(
            "Implement user account structure"
            "Implement login system"
            "Implement user session management"
            "Implement user shell"
            "Implement basic user utilities"
            "Validate user environment"
        )
    }

    20 = @{
        Title = "20 - Graphical Environment"
        Issues = @(
            "Implement graphics framebuffer support"
            "Implement graphics primitives"
            "Implement window management"
            "Implement mouse input"
            "Implement graphical desktop"
            "Implement graphical applications"
            "Validate graphical environment"
        )
    }

    21 = @{
        Title = "21 - OS Hardening & Reliability"
        Issues = @(
            "Implement kernel input validation"
            "Harden memory management"
            "Harden process isolation"
            "Implement kernel panic handling"
            "Implement resource cleanup"
            "Perform reliability testing"
        )
    }

    22 = @{
        Title = "22 - Testing & CI"
        Issues = @(
            "Expand kernel unit tests"
            "Implement integration tests"
            "Expand QEMU runtime testing"
            "Improve CI build validation"
            "Add regression testing"
            "Validate complete CI pipeline"
        )
    }

    23 = @{
        Title = "23 - Final InitraOS Architecture"
        Issues = @(
            "Integrate complete kernel architecture"
            "Integrate complete user-space architecture"
            "Integrate storage and filesystem"
            "Integrate graphical environment"
            "Perform complete system validation"
            "Document final InitraOS architecture"
            "Release final InitraOS architecture milestone"
        )
    }
}

# ============================================================
# Helper function for GitHub CLI commands
# ============================================================

function Invoke-GhCommand {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $Output = & gh @Arguments

    if ($LASTEXITCODE -ne 0) {
        throw "GitHub CLI command failed: gh $($Arguments -join ' ')"
    }

    return $Output
}

# ============================================================
# Authentication
# ============================================================

Write-Host ""
Write-Host "Checking GitHub authentication..." -ForegroundColor Cyan

Invoke-GhCommand @("auth", "status") | Out-Host

Write-Host "Authentication OK." -ForegroundColor Green
Write-Host ""

# ============================================================
# Get existing milestones
# ============================================================

Write-Host "Loading existing milestones..." -ForegroundColor Cyan

$MilestoneJson = Invoke-GhCommand @(
    "api",
    "repos/$Repo/milestones?state=open&per_page=100"
)

$MilestoneData = $MilestoneJson | ConvertFrom-Json

if (-not $MilestoneData) {
    throw "No milestones were returned by GitHub."
}

Write-Host "Milestones loaded successfully." -ForegroundColor Green
Write-Host ""

# ============================================================
# Get existing issues
# ============================================================

Write-Host "Loading existing issues..." -ForegroundColor Cyan

$IssueJson = Invoke-GhCommand @(
    "api",
    "repos/$Repo/issues?state=all&per_page=100"
)

$ExistingIssues = @($IssueJson | ConvertFrom-Json)

Write-Host "Existing issues loaded: $($ExistingIssues.Count)" -ForegroundColor Green
Write-Host ""

# ============================================================
# Get existing Project items
# ============================================================

Write-Host "Loading Project #$ProjectNumber items..." -ForegroundColor Cyan

$ProjectItemJson = Invoke-GhCommand @(
    "project",
    "item-list",
    "$ProjectNumber",
    "--owner",
    $ProjectOwner,
    "--format",
    "json",
    "--limit",
    "1000"
)

$ProjectItemData = $ProjectItemJson | ConvertFrom-Json

$ProjectItems = @($ProjectItemData.items)

Write-Host "Project items loaded: $($ProjectItems.Count)" -ForegroundColor Green
Write-Host ""

# ============================================================
# Process milestones 12-23
# ============================================================

foreach ($MilestoneNumber in ($Milestones.Keys | Sort-Object)) {

    $MilestoneInfo = $Milestones[$MilestoneNumber]
    $ExpectedTitle = $MilestoneInfo.Title

    Write-Host "==================================================" -ForegroundColor DarkGray
    Write-Host "Milestone #$MilestoneNumber" -ForegroundColor Cyan
    Write-Host "Expected: $ExpectedTitle" -ForegroundColor Cyan
    Write-Host "==================================================" -ForegroundColor DarkGray

    # --------------------------------------------------------
    # Find milestone by NUMBER
    # --------------------------------------------------------

    $Milestone = $MilestoneData |
        Where-Object {
            $_.number -eq $MilestoneNumber
        } |
        Select-Object -First 1

    if (-not $Milestone) {
        throw "Milestone #$MilestoneNumber was not found. Script will NOT create it."
    }

    Write-Host "Found: $($Milestone.title)" -ForegroundColor Green

    # --------------------------------------------------------
    # Process issues
    # --------------------------------------------------------

    foreach ($IssueTitle in $MilestoneInfo.Issues) {

        Write-Host ""
        Write-Host "Issue: $IssueTitle" -ForegroundColor White

        # ----------------------------------------------------
        # Check for existing issue
        # ----------------------------------------------------

        $ExistingIssue = $ExistingIssues |
            Where-Object {
                $_.title -eq $IssueTitle -and
                $_.milestone -ne $null -and
                $_.milestone.number -eq $MilestoneNumber
            } |
            Select-Object -First 1

        if ($ExistingIssue) {

            Write-Host "Already exists: #$($ExistingIssue.number)" -ForegroundColor Yellow

            $IssueNumber = $ExistingIssue.number
            $IssueUrl = $ExistingIssue.html_url

        }
        else {

            # ------------------------------------------------
            # Create issue
            # ------------------------------------------------

            Write-Host "Creating issue..." -ForegroundColor Cyan

            $NewIssueJson = Invoke-GhCommand @(
                "api",
                "--method",
                "POST",
                "repos/$Repo/issues",
                "-f",
                "title=$IssueTitle",
                "-F",
                "milestone=$MilestoneNumber"
            )

            $NewIssue = $NewIssueJson | ConvertFrom-Json

            if (-not $NewIssue.html_url) {
                throw "GitHub did not return a valid URL for '$IssueTitle'."
            }

            $IssueNumber = $NewIssue.number
            $IssueUrl = $NewIssue.html_url

            Write-Host "Created: #$IssueNumber" -ForegroundColor Green

            # Add to local cache
            $ExistingIssues += $NewIssue
        }

        # ----------------------------------------------------
        # Check whether issue is already in Project
        # ----------------------------------------------------

        $ProjectItem = $ProjectItems |
            Where-Object {
                $_.content.url -eq $IssueUrl
            } |
            Select-Object -First 1

        if ($ProjectItem) {

            Write-Host "Already in Project #$ProjectNumber." -ForegroundColor Yellow

        }
        else {

            Write-Host "Adding to Project #$ProjectNumber..." -ForegroundColor Cyan

            $AddedItemJson = Invoke-GhCommand @(
                "project",
                "item-add",
                "$ProjectNumber",
                "--owner",
                $ProjectOwner,
                "--url",
                $IssueUrl,
                "--format",
                "json"
            )

            $ProjectItem = $AddedItemJson | ConvertFrom-Json

            if (-not $ProjectItem.id) {
                throw "GitHub did not return a Project item ID for issue #$IssueNumber."
            }

            $ProjectItems += $ProjectItem

            Write-Host "Added to project." -ForegroundColor Green
        }

        # ----------------------------------------------------
        # Set Project Status = Todo
        # ----------------------------------------------------

        if (-not $ProjectItem.id) {
            throw "Project item ID is missing for issue #$IssueNumber."
        }

        Write-Host "Setting Project Status -> Todo..." -ForegroundColor Cyan

        Invoke-GhCommand @(
            "project",
            "item-edit",
            "--id",
            $ProjectItem.id,
            "--project-id",
            $ProjectId,
            "--field-id",
            $StatusFieldId,
            "--single-select-option-id",
            $TodoOptionId
        ) | Out-Null

        Write-Host "Status: Todo" -ForegroundColor Green
    }

    Write-Host ""
}

# ============================================================
# Final summary
# ============================================================

Write-Host ""
Write-Host "==================================================" -ForegroundColor Green
Write-Host "GITHUB SETUP COMPLETE" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Milestones 12-23 were NOT modified." -ForegroundColor White
Write-Host "Issues were created/verified." -ForegroundColor White
Write-Host "Issues were added/verified in Project #2." -ForegroundColor White
Write-Host "Project Status was set to Todo." -ForegroundColor White
Write-Host ""