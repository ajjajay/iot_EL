#!/usr/bin/env bash
# lambda/deploy.sh
# ─────────────────────────────────────────────────────────────────────────────
# Deploys both Lambda functions and creates the required AWS resources.
# Run once from the repo root: bash lambda/deploy.sh
#
# Prerequisites:
#   - AWS CLI configured (aws configure) with ap-south-1 access
#   - IAM role already created for Lambda (see ROLE_ARN below)
#   - FIREBASE_SERVICE_ACCOUNT_B64 exported in your shell
#     export FIREBASE_SERVICE_ACCOUNT_B64=$(base64 -w 0 serviceAccountKey.json)
# ─────────────────────────────────────────────────────────────────────────────

set -euo pipefail

REGION="ap-south-1"
ACCOUNT_ID=$(aws sts get-caller-identity --query Account --output text)
COLLECTION="iot-biometric-faces"
SNS_TOPIC="iot-biometric-alerts"
PIPELINE_FN="iot-biometric-pipeline"
ENROLL_FN="iot-enrollment-indexer"

# ── Env vars required in your shell ──────────────────────────────────────────
: "${FIREBASE_SERVICE_ACCOUNT_B64:?Set FIREBASE_SERVICE_ACCOUNT_B64}"
: "${FIREBASE_DATABASE_URL:?Set FIREBASE_DATABASE_URL}"

: "${FIREBASE_STORAGE_BUCKET:?Set FIREBASE_STORAGE_BUCKET}"
: "${IOT_ENDPOINT:?Set IOT_ENDPOINT (from AWS IoT Core → Settings → Endpoint)}"

# ── 1. Rekognition collection ─────────────────────────────────────────────────
echo "── Creating Rekognition collection: $COLLECTION"
aws rekognition create-collection \
    --collection-id "$COLLECTION" \
    --region "$REGION" 2>/dev/null && echo "  Created" || echo "  Already exists"

# ── 2. SNS topic ──────────────────────────────────────────────────────────────
echo "── Creating SNS topic: $SNS_TOPIC"
SNS_TOPIC_ARN=$(aws sns create-topic \
    --name "$SNS_TOPIC" \
    --region "$REGION" \
    --query TopicArn --output text)
echo "  ARN: $SNS_TOPIC_ARN"

# Subscribe your email (edit this)
: "${ADMIN_EMAIL:?Set ADMIN_EMAIL (your notification email)}"
aws sns subscribe \
    --topic-arn "$SNS_TOPIC_ARN" \
    --protocol email \
    --notification-endpoint "$ADMIN_EMAIL" \
    --region "$REGION" 2>/dev/null || true
echo "  Subscription email sent to $ADMIN_EMAIL (confirm it)"

# ── 3. IAM role for Lambda ────────────────────────────────────────────────────
ROLE_NAME="iot-lambda-execution-role"
echo "── Ensuring IAM role: $ROLE_NAME"
ROLE_ARN=$(aws iam get-role --role-name "$ROLE_NAME" --query Role.Arn --output text 2>/dev/null || \
  aws iam create-role \
    --role-name "$ROLE_NAME" \
    --assume-role-policy-document '{
      "Version":"2012-10-17",
      "Statement":[{"Effect":"Allow","Principal":{"Service":"lambda.amazonaws.com"},"Action":"sts:AssumeRole"}]
    }' \
    --query Role.Arn --output text)

aws iam attach-role-policy --role-name "$ROLE_NAME" \
    --policy-arn arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole 2>/dev/null || true
aws iam attach-role-policy --role-name "$ROLE_NAME" \
    --policy-arn arn:aws:iam::aws:policy/AmazonRekognitionFullAccess 2>/dev/null || true
aws iam attach-role-policy --role-name "$ROLE_NAME" \
    --policy-arn arn:aws:iam::aws:policy/AmazonSNSFullAccess 2>/dev/null || true

# Inline policy for IoT data publish
aws iam put-role-policy --role-name "$ROLE_NAME" \
    --policy-name iot-publish \
    --policy-document '{
      "Version":"2012-10-17",
      "Statement":[{"Effect":"Allow","Action":"iot:Publish","Resource":"*"}]
    }' 2>/dev/null || true

echo "  Role ARN: $ROLE_ARN"
echo "  Waiting 10s for role to propagate..."; sleep 10

# ── 4. Package + deploy biometric pipeline Lambda ─────────────────────────────
echo "── Deploying $PIPELINE_FN"
cd lambda/biometric_pipeline
pip install -r requirements.txt -t package/ -q
cp handler.py package/
cd package && zip -r ../function.zip . -q && cd ..

PIPELINE_ARN=$(aws lambda get-function --function-name "$PIPELINE_FN" \
    --region "$REGION" --query Configuration.FunctionArn --output text 2>/dev/null || echo "")

if [ -z "$PIPELINE_ARN" ]; then
  PIPELINE_ARN=$(aws lambda create-function \
    --function-name "$PIPELINE_FN" \
    --runtime python3.12 \
    --role "$ROLE_ARN" \
    --handler handler.handler \
    --timeout 30 \
    --memory-size 256 \
    --zip-file fileb://function.zip \
    --region "$REGION" \
    --query FunctionArn --output text)
  echo "  Created: $PIPELINE_ARN"
else
  aws lambda update-function-code \
    --function-name "$PIPELINE_FN" \
    --zip-file fileb://function.zip \
    --region "$REGION" --query FunctionArn --output text
  echo "  Updated: $PIPELINE_FN"
fi

aws lambda update-function-configuration \
    --function-name "$PIPELINE_FN" \
    --region "$REGION" \
    --environment "Variables={
        FIREBASE_DATABASE_URL=$FIREBASE_DATABASE_URL,
        FIREBASE_STORAGE_BUCKET=$FIREBASE_STORAGE_BUCKET,
        FIREBASE_SERVICE_ACCOUNT_B64=$FIREBASE_SERVICE_ACCOUNT_B64,
        SNS_TOPIC_ARN=$SNS_TOPIC_ARN,
        REKOGNITION_COLLECTION=$COLLECTION,
        REKOGNITION_MATCH_THRESHOLD=80,
        AWS_IOT_ENDPOINT=$IOT_ENDPOINT
    }" --query FunctionArn --output text > /dev/null

rm -rf package function.zip
cd ../..

# ── 5. Package + deploy enrollment indexer Lambda ─────────────────────────────
echo "── Deploying $ENROLL_FN"
cd lambda/enrollment_indexer
pip install -r requirements.txt -t package/ -q
cp handler.py package/
cd package && zip -r ../function.zip . -q && cd ..

ENROLL_ARN=$(aws lambda get-function --function-name "$ENROLL_FN" \
    --region "$REGION" --query Configuration.FunctionArn --output text 2>/dev/null || echo "")

if [ -z "$ENROLL_ARN" ]; then
  ENROLL_ARN=$(aws lambda create-function \
    --function-name "$ENROLL_FN" \
    --runtime python3.12 \
    --role "$ROLE_ARN" \
    --handler handler.handler \
    --timeout 30 \
    --memory-size 256 \
    --zip-file fileb://function.zip \
    --region "$REGION" \
    --query FunctionArn --output text)
  echo "  Created: $ENROLL_ARN"
else
  aws lambda update-function-code \
    --function-name "$ENROLL_FN" \
    --zip-file fileb://function.zip \
    --region "$REGION" --query FunctionArn --output text
  echo "  Updated: $ENROLL_FN"
fi

aws lambda update-function-configuration \
    --function-name "$ENROLL_FN" \
    --region "$REGION" \
    --environment "Variables={
        FIREBASE_DATABASE_URL=$FIREBASE_DATABASE_URL,
        FIREBASE_STORAGE_BUCKET=$FIREBASE_STORAGE_BUCKET,
        FIREBASE_SERVICE_ACCOUNT_B64=$FIREBASE_SERVICE_ACCOUNT_B64,
        REKOGNITION_COLLECTION=$COLLECTION
    }" --query FunctionArn --output text > /dev/null

rm -rf package function.zip
cd ../..

# ── 6. IoT Core permissions for Lambda ───────────────────────────────────────
echo "── Granting IoT Core permission to invoke Lambdas"
aws lambda add-permission \
    --function-name "$PIPELINE_FN" \
    --statement-id iot-biometric-signin \
    --action lambda:InvokeFunction \
    --principal iot.amazonaws.com \
    --region "$REGION" 2>/dev/null || true

aws lambda add-permission \
    --function-name "$ENROLL_FN" \
    --statement-id iot-biometric-enroll \
    --action lambda:InvokeFunction \
    --principal iot.amazonaws.com \
    --region "$REGION" 2>/dev/null || true

# ── 7. Print IoT Rule creation commands ──────────────────────────────────────
echo ""
echo "════════════════════════════════════════════════════════════"
echo "  Manual step: create these 2 IoT Rules in the AWS Console"
echo "  IoT Core → Message routing → Rules → Create rule"
echo "════════════════════════════════════════════════════════════"
echo ""
echo "Rule 1 — Biometric sign-in pipeline"
echo "  Name:  BiometricSigninPipeline"
echo "  SQL:   SELECT *, topic() AS mqttTopic FROM 'iot/+/biometric/signin'"
echo "  Action: Lambda → $PIPELINE_ARN"
echo ""
echo "Rule 2 — Enrollment Rekognition indexer"
echo "  Name:  EnrollmentIndexer"
echo "  SQL:   SELECT *, topic() AS mqttTopic FROM 'iot/+/biometric/enroll'"
echo "  Action: Lambda → $ENROLL_ARN"
echo ""
echo "════════════════════════════════════════════════════════════"
echo "  DONE. Confirm SNS subscription email before testing."
echo "════════════════════════════════════════════════════════════"
