// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/gtk/certificate_viewer.h"

#include <cert.h>
#include <gtk/gtk.h>
#include <hasht.h>
#include <sechash.h>

#include <algorithm>
#include <vector>

#include "app/l10n_util.h"
#include "base/gtk_util.h"
#include "base/i18n/time_formatting.h"
#include "base/nss_util.h"
#include "base/scoped_ptr.h"
#include "base/string_util.h"
#include "base/time.h"
#include "base/utf_string_conversions.h"
#include "chrome/browser/gtk/certificate_dialogs.h"
#include "chrome/browser/gtk/gtk_util.h"
#include "chrome/third_party/mozilla_security_manager/nsNSSCertHelper.h"
#include "chrome/third_party/mozilla_security_manager/nsNSSCertificate.h"
#include "chrome/third_party/mozilla_security_manager/nsUsageArrayHelper.h"
#include "grit/generated_resources.h"
#include "net/base/x509_certificate.h"

// PSM = Mozilla's Personal Security Manager.
namespace psm = mozilla_security_manager;

namespace {

const char kDetailsFontFamily[] = "monospace";

////////////////////////////////////////////////////////////////////////////////
// NSS utility functions.

// Convert a char* return value from NSS into a std::string and free the NSS
// memory.  If the arg is NULL, a "Field Not Present" string will be returned
// instead.
std::string Stringize(char* nss_text) {
  std::string s;
  if (nss_text) {
    s = nss_text;
    PORT_Free(nss_text);
  } else {
    s = l10n_util::GetStringUTF8(IDS_CERT_INFO_FIELD_NOT_PRESENT);
  }
  return s;
}

// Hash a certificate using the given algorithm, return the result as a
// colon-seperated hex string.  The len specified is the number of bytes
// required for storing the raw fingerprint.
// (It's a bit redundant that the caller needs to specify len in addition to the
// algorithm, but given the limited uses, not worth fixing.)
std::string HashCert(CERTCertificate* cert, HASH_HashType algorithm, int len) {
  unsigned char fingerprint[HASH_LENGTH_MAX];
  SECItem fingerprint_item;

  DCHECK(NULL != cert->derCert.data);
  DCHECK(0 != cert->derCert.len);
  DCHECK(len <= HASH_LENGTH_MAX);
  memset(fingerprint, 0, len);
  SECStatus rv = HASH_HashBuf(algorithm, fingerprint, cert->derCert.data,
                              cert->derCert.len);
  DCHECK(rv == SECSuccess);
  fingerprint_item.data = fingerprint;
  fingerprint_item.len = len;
  return Stringize(CERT_Hexify(&fingerprint_item, TRUE));
}

std::string ProcessSecAlgorithm(SECAlgorithmID* algorithm_id) {
  return psm::GetOIDText(&algorithm_id->algorithm);
}

std::string ProcessExtension(CERTCertExtension* extension) {
  std::string rv;
  int criticality = IDS_CERT_EXTENSION_NON_CRITICAL;
  if (extension->critical.data && extension->critical.data[0])
    criticality = IDS_CERT_EXTENSION_CRITICAL;
  rv = l10n_util::GetStringUTF8(criticality) + "\n" +
      psm::ProcessExtensionData(SECOID_FindOIDTag(&extension->id),
                                &extension->value);
  return rv;
}

////////////////////////////////////////////////////////////////////////////////
// Gtk utility functions.

void AddTitle(GtkTable* table, int row, const std::string& text) {
  gtk_table_attach_defaults(table,
                            gtk_util::CreateBoldLabel(text),
                            0, 2,
                            row, row + 1);
}

void AddKeyValue(GtkTable* table, int row, const std::string& text,
                 const std::string& value) {
  gtk_table_attach_defaults(
      table,
      gtk_util::IndentWidget(
          gtk_util::LeftAlignMisc(gtk_label_new(text.c_str()))),
      0, 1, row, row + 1);
  gtk_table_attach_defaults(
      table,
      gtk_util::LeftAlignMisc(gtk_label_new(value.c_str())),
      1, 2, row, row + 1);
}

////////////////////////////////////////////////////////////////////////////////
// CertificateViewer class definition.

class CertificateViewer {
 public:
  CertificateViewer(gfx::NativeWindow parent, CERTCertList* cert_chain_list);
  ~CertificateViewer();

  void InitGeneralPage();
  void InitDetailsPage();

  void Show();

 private:
  // Indices and column count for the certificate chain hierarchy tree store.
  enum {
    HIERARCHY_NAME,
    HIERARCHY_OBJECT,
    HIERARCHY_INDEX,
    HIERARCHY_COLUMNS
  };

  // Indices and column count for the certificate fields tree store.
  enum {
    FIELDS_NAME,
    FIELDS_VALUE,
    FIELDS_COLUMNS
  };

  // Fill the tree store with the certificate hierarchy, and set |leaf| to the
  // iter of the leaf node.
  void FillHierarchyStore(GtkTreeStore* hierarchy_store,
                          GtkTreeIter* leaf) const;

  // Fill the tree store with the details of the given certificate.
  static void FillTreeStoreWithCertFields(GtkTreeStore* store,
                                          CERTCertificate* cert);

  // Create a tree store filled with the details of the given certificate.
  static GtkTreeStore* CreateFieldsTreeStore(CERTCertificate* cert);

  // Callbacks for user selecting elements in the trees.
  static void OnHierarchySelectionChanged(GtkTreeSelection* selection,
                                          CertificateViewer* viewer);
  static void OnFieldsSelectionChanged(GtkTreeSelection* selection,
                                       CertificateViewer* viewer);

  // Callback for export button.
  static void OnExportClicked(GtkButton *button, CertificateViewer* viewer);

  // The certificate hierarchy (leaf cert first).
  CERTCertList* cert_chain_list_;
  // The same contents of cert_chain_list_ in a vector for easier access.
  typedef std::vector<CERTCertificate*> CertificateVector;
  CertificateVector cert_chain_;

  GtkWidget* dialog_;
  GtkWidget* notebook_;
  GtkWidget* general_page_vbox_;
  GtkWidget* details_page_vbox_;
  GtkTreeSelection* hierarchy_selection_;
  GtkWidget* fields_tree_;
  GtkTextBuffer* field_value_buffer_;
  GtkWidget* export_button_;

  DISALLOW_COPY_AND_ASSIGN(CertificateViewer);
};

////////////////////////////////////////////////////////////////////////////////
// CertificateViewer implementation.

// Close button callback.
void OnDialogResponse(GtkDialog* dialog, gint response_id,
                      gpointer user_data) {
  // "Close" was clicked.
  gtk_widget_destroy(GTK_WIDGET(dialog));
}

void OnDestroy(GtkDialog* dialog, CertificateViewer* cert_viewer) {
  delete cert_viewer;
}

CertificateViewer::CertificateViewer(gfx::NativeWindow parent,
                                     CERTCertList* cert_chain_list)
    : cert_chain_list_(cert_chain_list) {
  CERTCertListNode* node;
  for (node = CERT_LIST_HEAD(cert_chain_list_);
       !CERT_LIST_END(node, cert_chain_list_);
       node = CERT_LIST_NEXT(node)) {
    cert_chain_.push_back(node->cert);
  }

  dialog_ = gtk_dialog_new_with_buttons(
      l10n_util::GetStringFUTF8(
          IDS_CERT_INFO_DIALOG_TITLE,
          UTF8ToUTF16(psm::GetCertTitle(cert_chain_.front()))).c_str(),
      parent,
      // Non-modal.
      GTK_DIALOG_NO_SEPARATOR,
      GTK_STOCK_CLOSE,
      GTK_RESPONSE_CLOSE,
      NULL);
  gtk_box_set_spacing(GTK_BOX(GTK_DIALOG(dialog_)->vbox),
                      gtk_util::kContentAreaSpacing);

  psm::RegisterDynamicOids();
  InitGeneralPage();
  InitDetailsPage();

  notebook_ = gtk_notebook_new();
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dialog_)->vbox), notebook_);

  gtk_notebook_append_page(
      GTK_NOTEBOOK(notebook_),
      general_page_vbox_,
      gtk_label_new_with_mnemonic(
          gtk_util::ConvertAcceleratorsFromWindowsStyle(
              l10n_util::GetStringUTF8(
                  IDS_CERT_INFO_GENERAL_TAB_LABEL)).c_str()));

  gtk_notebook_append_page(
      GTK_NOTEBOOK(notebook_),
      details_page_vbox_,
      gtk_label_new_with_mnemonic(
          gtk_util::ConvertAcceleratorsFromWindowsStyle(
              l10n_util::GetStringUTF8(
                  IDS_CERT_INFO_DETAILS_TAB_LABEL)).c_str()));

  g_signal_connect(dialog_, "response", G_CALLBACK(OnDialogResponse), NULL);
  g_signal_connect(dialog_, "destroy", G_CALLBACK(OnDestroy), this);
}

CertificateViewer::~CertificateViewer() {
  CERT_DestroyCertList(cert_chain_list_);
}

void CertificateViewer::InitGeneralPage() {
  CERTCertificate* cert = cert_chain_.front();
  general_page_vbox_ = gtk_vbox_new(FALSE, gtk_util::kContentAreaSpacing);
  gtk_container_set_border_width(GTK_CONTAINER(general_page_vbox_),
                                 gtk_util::kContentAreaBorder);

  GtkWidget* uses_vbox = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(general_page_vbox_), uses_vbox, FALSE, FALSE, 0);
  gtk_box_pack_start(
      GTK_BOX(uses_vbox),
      gtk_util::CreateBoldLabel(
          l10n_util::GetStringUTF8(IDS_CERT_INFO_VERIFIED_USAGES_GROUP)),
      FALSE, FALSE, 0);

  std::vector<std::string> usages;
  psm::GetCertUsageStrings(cert, &usages);
  for (size_t i = 0; i < usages.size(); ++i)
    gtk_box_pack_start(
        GTK_BOX(uses_vbox),
        gtk_util::IndentWidget(gtk_util::LeftAlignMisc(gtk_label_new(
            usages[i].c_str()))),
        FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(general_page_vbox_), gtk_hseparator_new(),
                     FALSE, FALSE, 0);

  const int num_rows = 21;
  GtkTable* table = GTK_TABLE(gtk_table_new(num_rows, 2, FALSE));
  gtk_table_set_col_spacing(table, 0, gtk_util::kLabelSpacing);
  gtk_table_set_row_spacings(table, gtk_util::kControlSpacing);

  gtk_box_pack_start(GTK_BOX(general_page_vbox_), GTK_WIDGET(table),
                     FALSE, FALSE, 0);
  int row = 0;
  AddTitle(table, row++,
           l10n_util::GetStringUTF8(IDS_CERT_INFO_SUBJECT_GROUP));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_COMMON_NAME_LABEL),
              psm::ProcessIDN(Stringize(CERT_GetCommonName(&cert->subject))));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_ORGANIZATION_LABEL),
              Stringize(CERT_GetOrgName(&cert->subject)));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_ORGANIZATIONAL_UNIT_LABEL),
              Stringize(CERT_GetOrgUnitName(&cert->subject)));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_SERIAL_NUMBER_LABEL),
              Stringize(CERT_Hexify(&cert->serialNumber, TRUE)));

  row += 2;  // Add spacing (kControlSpacing * 3 == kContentAreaSpacing).

  AddTitle(table, row++,
           l10n_util::GetStringUTF8(IDS_CERT_INFO_ISSUER_GROUP));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_COMMON_NAME_LABEL),
              psm::ProcessIDN(Stringize(CERT_GetCommonName(&cert->issuer))));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_ORGANIZATION_LABEL),
              Stringize(CERT_GetOrgName(&cert->issuer)));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_ORGANIZATIONAL_UNIT_LABEL),
              Stringize(CERT_GetOrgUnitName(&cert->issuer)));

  row += 2;  // Add spacing (kControlSpacing * 3 == kContentAreaSpacing).

  PRTime issued, expires;
  std::string issued_str, expires_str;
  if (CERT_GetCertTimes(cert, &issued, &expires) == SECSuccess) {
    issued_str = WideToUTF8(
        base::TimeFormatShortDateNumeric(base::PRTimeToBaseTime(issued)));
    expires_str = WideToUTF8(
        base::TimeFormatShortDateNumeric(base::PRTimeToBaseTime(expires)));
  } else {
    issued_str = l10n_util::GetStringUTF8(IDS_CERT_INFO_FIELD_NOT_PRESENT);
    expires_str = l10n_util::GetStringUTF8(IDS_CERT_INFO_FIELD_NOT_PRESENT);
  }
  AddTitle(table, row++,
           l10n_util::GetStringUTF8(IDS_CERT_INFO_VALIDITY_GROUP));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_ISSUED_ON_LABEL),
              issued_str);
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_EXPIRES_ON_LABEL),
              expires_str);

  row += 2;  // Add spacing (kControlSpacing * 3 == kContentAreaSpacing).

  AddTitle(table, row++,
           l10n_util::GetStringUTF8(IDS_CERT_INFO_FINGERPRINTS_GROUP));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_SHA1_FINGERPRINT_LABEL),
              HashCert(cert, HASH_AlgSHA1, SHA1_LENGTH));
  AddKeyValue(table, row++,
              l10n_util::GetStringUTF8(IDS_CERT_INFO_MD5_FINGERPRINT_LABEL),
              HashCert(cert, HASH_AlgMD5, MD5_LENGTH));

  DCHECK_EQ(row, num_rows);
}

void CertificateViewer::FillHierarchyStore(GtkTreeStore* hierarchy_store,
                                           GtkTreeIter* leaf) const {
  GtkTreeIter parent;
  GtkTreeIter* parent_ptr = NULL;
  GtkTreeIter iter;
  gint index = cert_chain_.size() - 1;
  for (CertificateVector::const_reverse_iterator i = cert_chain_.rbegin();
       i != cert_chain_.rend(); ++i, --index) {
    gtk_tree_store_append(hierarchy_store, &iter, parent_ptr);
    GtkTreeStore* fields_store = CreateFieldsTreeStore(*i);
    gtk_tree_store_set(
        hierarchy_store, &iter,
        HIERARCHY_NAME, psm::GetCertTitle(*i).c_str(),
        HIERARCHY_OBJECT, fields_store,
        HIERARCHY_INDEX, index,
        -1);
    g_object_unref(fields_store);
    parent = iter;
    parent_ptr = &parent;
  }
  *leaf = iter;
}

// static
void CertificateViewer::FillTreeStoreWithCertFields(GtkTreeStore* store,
                                                    CERTCertificate* cert) {
  GtkTreeIter top;
  gtk_tree_store_append(store, &top, NULL);
  gtk_tree_store_set(
      store, &top,
      FIELDS_NAME, psm::GetCertTitle(cert).c_str(),
      FIELDS_VALUE, "",
      -1);

  GtkTreeIter cert_iter;
  gtk_tree_store_append(store, &cert_iter, &top);
  gtk_tree_store_set(
      store, &cert_iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_CERTIFICATE).c_str(),
      FIELDS_VALUE, "",
      -1);

  unsigned long version = ULONG_MAX;
  std::string version_str;
  if (SEC_ASN1DecodeInteger(&cert->version, &version) == SECSuccess &&
      version != ULONG_MAX)
    version_str = l10n_util::GetStringFUTF8(IDS_CERT_DETAILS_VERSION_FORMAT,
                                            UintToString16(version + 1));
  GtkTreeIter iter;
  gtk_tree_store_append(store, &iter, &cert_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_VERSION).c_str(),
      FIELDS_VALUE, version_str.c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &cert_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_SERIAL_NUMBER).c_str(),
      FIELDS_VALUE, Stringize(CERT_Hexify(&cert->serialNumber, TRUE)).c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &cert_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_CERTIFICATE_SIG_ALG).c_str(),
      FIELDS_VALUE, ProcessSecAlgorithm(&cert->signature).c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &cert_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_ISSUER).c_str(),
      FIELDS_VALUE, psm::ProcessName(&cert->issuer).c_str(),
      -1);

  GtkTreeIter validity_iter;
  gtk_tree_store_append(store, &validity_iter, &cert_iter);
  gtk_tree_store_set(
      store, &validity_iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_VALIDITY).c_str(),
      FIELDS_VALUE, "",
      -1);

  PRTime issued, expires;
  std::string issued_str, expires_str;
  if (CERT_GetCertTimes(cert, &issued, &expires) == SECSuccess) {
    issued_str = WideToUTF8(
        base::TimeFormatShortDateAndTime(base::PRTimeToBaseTime(issued)));
    expires_str = WideToUTF8(
        base::TimeFormatShortDateAndTime(base::PRTimeToBaseTime(expires)));
  }
  gtk_tree_store_append(store, &iter, &validity_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_NOT_BEFORE).c_str(),
      FIELDS_VALUE, issued_str.c_str(),
      -1);
  gtk_tree_store_append(store, &iter, &validity_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_NOT_AFTER).c_str(),
      FIELDS_VALUE, expires_str.c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &cert_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_SUBJECT).c_str(),
      FIELDS_VALUE, psm::ProcessName(&cert->subject).c_str(),
      -1);

  GtkTreeIter subject_public_key_iter;
  gtk_tree_store_append(store, &subject_public_key_iter, &cert_iter);
  gtk_tree_store_set(
      store, &subject_public_key_iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_SUBJECT_KEY_INFO).c_str(),
      FIELDS_VALUE, "",
      -1);

  gtk_tree_store_append(store, &iter, &subject_public_key_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_SUBJECT_KEY_ALG).c_str(),
      FIELDS_VALUE,
      ProcessSecAlgorithm(&cert->subjectPublicKeyInfo.algorithm).c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &subject_public_key_iter);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_SUBJECT_KEY).c_str(),
      FIELDS_VALUE,
      psm::ProcessSubjectPublicKeyInfo(&cert->subjectPublicKeyInfo).c_str(),
      -1);

  if (cert->extensions) {
    GtkTreeIter extensions_iter;
    gtk_tree_store_append(store, &extensions_iter, &cert_iter);
    gtk_tree_store_set(
        store, &extensions_iter,
        FIELDS_NAME,
        l10n_util::GetStringUTF8(IDS_CERT_DETAILS_EXTENSIONS).c_str(),
        FIELDS_VALUE, "",
        -1);

    for (size_t i = 0; cert->extensions[i] != NULL; ++i) {
      gtk_tree_store_append(store, &iter, &extensions_iter);
      gtk_tree_store_set(
          store, &iter,
          FIELDS_NAME, psm::GetOIDText(&cert->extensions[i]->id).c_str(),
          FIELDS_VALUE, ProcessExtension(cert->extensions[i]).c_str(),
          -1);
    }
  }

  gtk_tree_store_append(store, &iter, &top);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_CERTIFICATE_SIG_ALG).c_str(),
      FIELDS_VALUE,
      ProcessSecAlgorithm(&cert->signatureWrap.signatureAlgorithm).c_str(),
      -1);

  gtk_tree_store_append(store, &iter, &top);
  gtk_tree_store_set(
      store, &iter,
      FIELDS_NAME,
      l10n_util::GetStringUTF8(IDS_CERT_DETAILS_CERTIFICATE_SIG_VALUE).c_str(),
      FIELDS_VALUE, psm::ProcessRawBits(&cert->signatureWrap.signature).c_str(),
      -1);
}

// static
GtkTreeStore* CertificateViewer::CreateFieldsTreeStore(CERTCertificate* cert) {
  GtkTreeStore* fields_store = gtk_tree_store_new(FIELDS_COLUMNS, G_TYPE_STRING,
                                                  G_TYPE_STRING);
  FillTreeStoreWithCertFields(fields_store, cert);
  return fields_store;
}

void CertificateViewer::InitDetailsPage() {
  details_page_vbox_ = gtk_vbox_new(FALSE, gtk_util::kContentAreaSpacing);
  gtk_container_set_border_width(GTK_CONTAINER(details_page_vbox_),
                                 gtk_util::kContentAreaBorder);

  GtkWidget* hierarchy_vbox = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(details_page_vbox_), hierarchy_vbox,
                     FALSE, FALSE, 0);

  gtk_box_pack_start(GTK_BOX(hierarchy_vbox),
                     gtk_util::CreateBoldLabel(l10n_util::GetStringUTF8(
                         IDS_CERT_DETAILS_CERTIFICATE_HIERARCHY_LABEL)),
                     FALSE, FALSE, 0);

  GtkTreeStore* hierarchy_store = gtk_tree_store_new(HIERARCHY_COLUMNS,
                                                     G_TYPE_STRING,
                                                     G_TYPE_OBJECT,
                                                     G_TYPE_INT);
  GtkTreeIter hierarchy_leaf_iter;
  FillHierarchyStore(hierarchy_store, &hierarchy_leaf_iter);
  GtkWidget* hierarchy_tree = gtk_tree_view_new_with_model(
      GTK_TREE_MODEL(hierarchy_store));
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(hierarchy_tree), FALSE);
  gtk_tree_view_append_column(
      GTK_TREE_VIEW(hierarchy_tree),
      gtk_tree_view_column_new_with_attributes("", gtk_cell_renderer_text_new(),
                                               "text", HIERARCHY_NAME,
                                               NULL));
  gtk_tree_view_expand_all(GTK_TREE_VIEW(hierarchy_tree));
  hierarchy_selection_ = gtk_tree_view_get_selection(
      GTK_TREE_VIEW(hierarchy_tree));
  gtk_tree_selection_set_mode(hierarchy_selection_, GTK_SELECTION_SINGLE);
  g_signal_connect(hierarchy_selection_, "changed",
                   G_CALLBACK(OnHierarchySelectionChanged), this);
  GtkWidget* hierarchy_scroll_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(hierarchy_scroll_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_NEVER);
  gtk_scrolled_window_set_shadow_type(
      GTK_SCROLLED_WINDOW(hierarchy_scroll_window), GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(hierarchy_scroll_window), hierarchy_tree);
  gtk_box_pack_start(GTK_BOX(hierarchy_vbox),
                     hierarchy_scroll_window, FALSE, FALSE, 0);

  GtkWidget* fields_vbox = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(details_page_vbox_), fields_vbox,
                     TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(fields_vbox),
                     gtk_util::CreateBoldLabel(l10n_util::GetStringUTF8(
                         IDS_CERT_DETAILS_CERTIFICATE_FIELDS_LABEL)),
                     FALSE, FALSE, 0);

  fields_tree_ = gtk_tree_view_new();
  gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(fields_tree_), FALSE);
  gtk_tree_view_append_column(
      GTK_TREE_VIEW(fields_tree_),
      gtk_tree_view_column_new_with_attributes("", gtk_cell_renderer_text_new(),
                                               "text", FIELDS_NAME,
                                               NULL));
  GtkTreeSelection* fields_selection = gtk_tree_view_get_selection(
      GTK_TREE_VIEW(fields_tree_));
  gtk_tree_selection_set_mode(fields_selection, GTK_SELECTION_SINGLE);
  g_signal_connect(fields_selection, "changed",
                   G_CALLBACK(OnFieldsSelectionChanged), this);
  GtkWidget* fields_scroll_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(fields_scroll_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(
      GTK_SCROLLED_WINDOW(fields_scroll_window), GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(fields_scroll_window), fields_tree_);
  gtk_box_pack_start(GTK_BOX(fields_vbox),
                     fields_scroll_window, TRUE, TRUE, 0);

  GtkWidget* value_vbox = gtk_vbox_new(FALSE, gtk_util::kControlSpacing);
  gtk_box_pack_start(GTK_BOX(details_page_vbox_), value_vbox,
                     TRUE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(value_vbox),
                     gtk_util::CreateBoldLabel(l10n_util::GetStringUTF8(
                         IDS_CERT_DETAILS_CERTIFICATE_FIELD_VALUE_LABEL)),
                     FALSE, FALSE, 0);

  // TODO(mattm): fix text view coloring (should have grey background).
  GtkWidget* field_value_view = gtk_text_view_new();
  gtk_text_view_set_editable(GTK_TEXT_VIEW(field_value_view), FALSE);
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(field_value_view), GTK_WRAP_NONE);
  field_value_buffer_ = gtk_text_view_get_buffer(
      GTK_TEXT_VIEW(field_value_view));
  GtkWidget* value_scroll_window = gtk_scrolled_window_new(NULL, NULL);
  gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(value_scroll_window),
                                 GTK_POLICY_AUTOMATIC,
                                 GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type(
      GTK_SCROLLED_WINDOW(value_scroll_window), GTK_SHADOW_ETCHED_IN);
  gtk_container_add(GTK_CONTAINER(value_scroll_window), field_value_view);
  gtk_box_pack_start(GTK_BOX(value_vbox),
                     value_scroll_window, TRUE, TRUE, 0);

  gtk_widget_ensure_style(field_value_view);
  PangoFontDescription* font_desc = pango_font_description_copy(
      gtk_widget_get_style(field_value_view)->font_desc);
  pango_font_description_set_family(font_desc, kDetailsFontFamily);
  gtk_widget_modify_font(field_value_view, font_desc);
  pango_font_description_free(font_desc);

  GtkWidget* export_hbox = gtk_hbox_new(FALSE, 0);
  gtk_box_pack_start(GTK_BOX(details_page_vbox_), export_hbox,
                     FALSE, FALSE, 0);
  export_button_ = gtk_button_new_with_mnemonic(
      gtk_util::ConvertAcceleratorsFromWindowsStyle(
          l10n_util::GetStringUTF8(
              IDS_CERT_DETAILS_EXPORT_CERTIFICATE)).c_str());
  g_signal_connect(export_button_, "clicked",
                   G_CALLBACK(OnExportClicked), this);
  gtk_box_pack_start(GTK_BOX(export_hbox), export_button_,
                     FALSE, FALSE, 0);

  // Select the initial certificate in the hierarchy.
  gtk_tree_selection_select_iter(hierarchy_selection_, &hierarchy_leaf_iter);
}

// static
void CertificateViewer::OnHierarchySelectionChanged(
    GtkTreeSelection* selection, CertificateViewer* viewer) {
  GtkTreeIter iter;
  GtkTreeModel* model;
  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    GtkTreeStore* fields_store = NULL;
    gtk_tree_model_get(model, &iter, HIERARCHY_OBJECT, &fields_store, -1);
    gtk_tree_view_set_model(GTK_TREE_VIEW(viewer->fields_tree_),
                            GTK_TREE_MODEL(fields_store));
    gtk_tree_view_expand_all(GTK_TREE_VIEW(viewer->fields_tree_));
    gtk_widget_set_sensitive(viewer->export_button_, TRUE);
  } else {
    gtk_tree_view_set_model(GTK_TREE_VIEW(viewer->fields_tree_), NULL);
    gtk_widget_set_sensitive(viewer->export_button_, FALSE);
  }
}

// static
void CertificateViewer::OnFieldsSelectionChanged(GtkTreeSelection* selection,
                                                 CertificateViewer* viewer) {
  GtkTreeIter iter;
  GtkTreeModel* model;
  if (gtk_tree_selection_get_selected(selection, &model, &iter)) {
    gchar* value_string = NULL;
    gtk_tree_model_get(model, &iter, FIELDS_VALUE, &value_string, -1);
    if (value_string) {
      gtk_text_buffer_set_text(viewer->field_value_buffer_, value_string, -1);
      g_free(value_string);
    } else {
      gtk_text_buffer_set_text(viewer->field_value_buffer_, "", 0);
    }
  } else {
    gtk_text_buffer_set_text(viewer->field_value_buffer_, "", 0);
  }
}

// static
void CertificateViewer::OnExportClicked(GtkButton *button,
                                        CertificateViewer* viewer) {
  GtkTreeIter iter;
  GtkTreeModel* model;
  if (!gtk_tree_selection_get_selected(viewer->hierarchy_selection_, &model,
                                       &iter))
    return;
  gint cert_index = -1;
  gtk_tree_model_get(model, &iter, HIERARCHY_INDEX, &cert_index, -1);

  if (cert_index < 0) {
    NOTREACHED();
    return;
  }

  ShowCertExportDialog(GTK_WINDOW(viewer->dialog_),
                       viewer->cert_chain_[cert_index]);
}

void CertificateViewer::Show() {
  gtk_util::ShowDialog(dialog_);
}

} // namespace

void ShowCertificateViewer(gfx::NativeWindow parent, CERTCertificate* cert) {
  CERTCertList* cert_chain = CERT_GetCertChainFromCert(
      cert, PR_Now(), certUsageSSLServer);
  DCHECK(cert_chain);
  (new CertificateViewer(parent, cert_chain))->Show();
}

void ShowCertificateViewer(gfx::NativeWindow parent,
                           net::X509Certificate* cert) {
  ShowCertificateViewer(parent, cert->os_cert_handle());
}
