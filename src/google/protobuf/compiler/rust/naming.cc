// Protocol Buffers - Google's data interchange format
// Copyright 2023 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "google/protobuf/compiler/rust/naming.h"

#include <string>
#include <vector>

#include "absl/log/absl_log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_replace.h"
#include "absl/strings/str_split.h"
#include "absl/strings/string_view.h"
#include "absl/strings/substitute.h"
#include "google/protobuf/compiler/code_generator.h"
#include "google/protobuf/compiler/cpp/helpers.h"
#include "google/protobuf/compiler/rust/context.h"
#include "google/protobuf/descriptor.h"

namespace google {
namespace protobuf {
namespace compiler {
namespace rust {
namespace {
std::string GetUnderscoreDelimitedFullName(Context& ctx,
                                           const Descriptor& msg) {
  std::string result = msg.full_name();
  absl::StrReplaceAll({{".", "_"}}, &result);
  return result;
}
}  // namespace

std::string GetCrateName(Context& ctx, const FileDescriptor& dep) {
  absl::string_view path = dep.name();
  auto basename = path.substr(path.rfind('/') + 1);
  return absl::StrReplaceAll(basename, {{".", "_"}, {"-", "_"}});
}

std::string GetRsFile(Context& ctx, const FileDescriptor& file) {
  auto basename = StripProto(file.name());
  switch (auto k = ctx.opts().kernel) {
    case Kernel::kUpb:
      return absl::StrCat(basename, ".u.pb.rs");
    case Kernel::kCpp:
      return absl::StrCat(basename, ".c.pb.rs");
    default:
      ABSL_LOG(FATAL) << "Unknown kernel type: " << static_cast<int>(k);
      return "";
  }
}

std::string GetThunkCcFile(Context& ctx, const FileDescriptor& file) {
  auto basename = StripProto(file.name());
  return absl::StrCat(basename, ".pb.thunks.cc");
}

std::string GetHeaderFile(Context& ctx, const FileDescriptor& file) {
  auto basename = StripProto(file.name());
  return absl::StrCat(basename, ".proto.h");
}

namespace {

template <typename T>
std::string FieldPrefix(Context& ctx, const T& field) {
  // NOTE: When ctx.is_upb(), this functions outputs must match the symbols
  // that the upbc plugin generates exactly. Failure to do so correctly results
  // in a link-time failure.
  absl::string_view prefix = ctx.is_cpp() ? "__rust_proto_thunk__" : "";
  std::string thunk_prefix = absl::StrCat(
      prefix, GetUnderscoreDelimitedFullName(ctx, *field.containing_type()));
  return thunk_prefix;
}

template <typename T>
std::string ThunkName(Context& ctx, const T& field, absl::string_view op) {
  std::string thunkName = FieldPrefix(ctx, field);

  absl::string_view format;
  if (ctx.is_upb() && op == "get") {
    // upb getter is simply the field name (no "get" in the name).
    format = "_$1";
  } else if (ctx.is_upb() && op == "get_mut") {
    // same as above, with with `mutable` prefix
    format = "_mutable_$1";
  } else if (ctx.is_upb() && op == "case") {
    // some upb functions are in the order x_op compared to has/set/clear which
    // are in the other order e.g. op_x.
    format = "_$1_$0";
  } else {
    format = "_$0_$1";
  }

  absl::SubstituteAndAppend(&thunkName, format, op, field.name());
  return thunkName;
}

std::string ThunkMapOrRepeated(Context& ctx, const FieldDescriptor& field,
                               absl::string_view op) {
  if (!ctx.is_upb()) {
    return ThunkName<FieldDescriptor>(ctx, field, op);
  }

  std::string thunkName = absl::StrCat("_", FieldPrefix(ctx, field));
  absl::string_view format;
  if (op == "get") {
    format = field.is_map() ? "_$1_upb_map" : "_$1_upb_array";
  } else if (op == "get_mut") {
    format = field.is_map() ? "_$1_mutable_upb_map" : "_$1_mutable_upb_array";
  } else {
    return ThunkName<FieldDescriptor>(ctx, field, op);
  }

  absl::SubstituteAndAppend(&thunkName, format, op, field.name());
  return thunkName;
}

}  // namespace

std::string ThunkName(Context& ctx, const FieldDescriptor& field,
                      absl::string_view op) {
  if (field.is_map() || field.is_repeated()) {
    return ThunkMapOrRepeated(ctx, field, op);
  }
  return ThunkName<FieldDescriptor>(ctx, field, op);
}

std::string ThunkName(Context& ctx, const OneofDescriptor& field,
                      absl::string_view op) {
  return ThunkName<OneofDescriptor>(ctx, field, op);
}

std::string ThunkName(Context& ctx, const Descriptor& msg,
                      absl::string_view op) {
  absl::string_view prefix = ctx.is_cpp() ? "__rust_proto_thunk__" : "";
  return absl::StrCat(prefix, GetUnderscoreDelimitedFullName(ctx, msg), "_",
                      op);
}

std::string PrimitiveRsTypeName(const FieldDescriptor& field) {
  switch (field.type()) {
    case FieldDescriptor::TYPE_BOOL:
      return "bool";
    case FieldDescriptor::TYPE_INT32:
    case FieldDescriptor::TYPE_SINT32:
    case FieldDescriptor::TYPE_SFIXED32:
      return "i32";
    case FieldDescriptor::TYPE_INT64:
    case FieldDescriptor::TYPE_SINT64:
    case FieldDescriptor::TYPE_SFIXED64:
      return "i64";
    case FieldDescriptor::TYPE_FIXED32:
    case FieldDescriptor::TYPE_UINT32:
      return "u32";
    case FieldDescriptor::TYPE_FIXED64:
    case FieldDescriptor::TYPE_UINT64:
      return "u64";
    case FieldDescriptor::TYPE_FLOAT:
      return "f32";
    case FieldDescriptor::TYPE_DOUBLE:
      return "f64";
    case FieldDescriptor::TYPE_BYTES:
      return "[u8]";
    case FieldDescriptor::TYPE_STRING:
      return "::__pb::ProtoStr";
    default:
      break;
  }
  ABSL_LOG(FATAL) << "Unsupported field type: " << field.type_name();
  return "";
}

// Constructs a string of the Rust modules which will contain the message.
//
// Example: Given a message 'NestedMessage' which is defined in package 'x.y'
// which is inside 'ParentMessage', the message will be placed in the
// x::y::ParentMessage_ Rust module, so this function will return the string
// "x::y::ParentMessage_::".
//
// If the message has no package and no containing messages then this returns
// empty string.
std::string RustModule(Context& ctx, const Descriptor& msg) {
  std::vector<std::string> modules;

  std::vector<std::string> package_modules =
      absl::StrSplit(msg.file()->package(), '.', absl::SkipEmpty());

  modules.insert(modules.begin(), package_modules.begin(),
                 package_modules.end());

  // Innermost to outermost order.
  std::vector<std::string> modules_from_containing_types;
  const Descriptor* parent = msg.containing_type();
  while (parent != nullptr) {
    modules_from_containing_types.push_back(absl::StrCat(parent->name(), "_"));
    parent = parent->containing_type();
  }

  // Add the modules from containing messages (rbegin/rend to get them in outer
  // to inner order).
  modules.insert(modules.end(), modules_from_containing_types.rbegin(),
                 modules_from_containing_types.rend());

  // If there is any modules at all, push an empty string on the end so that
  // we get the trailing ::
  if (!modules.empty()) {
    modules.push_back("");
  }

  return absl::StrJoin(modules, "::");
}

std::string RustInternalModuleName(Context& ctx, const FileDescriptor& file) {
  return absl::StrReplaceAll(StripProto(file.name()),
                             {{"_", "__"}, {"/", "_s"}});
}

std::string GetCrateRelativeQualifiedPath(Context& ctx, const Descriptor& msg) {
  return absl::StrCat(RustModule(ctx, msg), msg.name());
}

std::string FieldInfoComment(Context& ctx, const FieldDescriptor& field) {
  absl::string_view label = field.is_repeated() ? "repeated" : "optional";
  std::string comment = absl::StrCat(field.name(), ": ", label, " ",
                                     FieldDescriptor::TypeName(field.type()));

  if (auto* m = field.message_type()) {
    absl::StrAppend(&comment, " ", m->full_name());
  }
  if (auto* m = field.enum_type()) {
    absl::StrAppend(&comment, " ", m->full_name());
  }

  return comment;
}

std::string EnumRsName(const EnumDescriptor& desc) {
  return SnakeToUpperCamelCase(desc.name());
}

std::string OneofViewEnumRsName(const OneofDescriptor& oneof) {
  return SnakeToUpperCamelCase(oneof.name());
}

std::string OneofMutEnumRsName(const OneofDescriptor& oneof) {
  return SnakeToUpperCamelCase(oneof.name()) + "Mut";
}

std::string OneofCaseEnumRsName(const OneofDescriptor& oneof) {
  // Note: This is the name used for the cpp Case enum, we use it for both
  // the Rust Case enum as well as for the cpp case enum in the cpp thunk.
  return SnakeToUpperCamelCase(oneof.name()) + "Case";
}

std::string OneofCaseRsName(const FieldDescriptor& oneof_field) {
  return SnakeToUpperCamelCase(oneof_field.name());
}

std::string CamelToSnakeCase(absl::string_view input) {
  std::string result;
  result.reserve(input.size() + 4);  // No reallocation for 4 _
  bool is_first_character = true;
  bool last_char_was_underscore = false;
  for (const char c : input) {
    if (!is_first_character && absl::ascii_isupper(c) &&
        !last_char_was_underscore) {
      result += '_';
    }
    last_char_was_underscore = c == '_';
    result += absl::ascii_tolower(c);
    is_first_character = false;
  }
  return result;
}

std::string SnakeToUpperCamelCase(absl::string_view input) {
  return cpp::UnderscoresToCamelCase(input, /*cap first letter=*/true);
}

std::string ScreamingSnakeToUpperCamelCase(absl::string_view input) {
  std::string result;
  bool cap_next_letter = true;
  for (const char c : input) {
    if (absl::ascii_isalpha(c)) {
      if (cap_next_letter) {
        result += absl::ascii_toupper(c);
      } else {
        result += absl::ascii_tolower(c);
      }
      cap_next_letter = false;
    } else if (absl::ascii_isdigit(c)) {
      result += c;
      cap_next_letter = true;
    } else {
      cap_next_letter = true;
    }
  }
  return result;
}

}  // namespace rust
}  // namespace compiler
}  // namespace protobuf
}  // namespace google
